#include "ac_controller.h"
#include "input_utils.h"
#include <cstring>

namespace {
constexpr uint32_t kAcStateVersion = 1;
bool validNormalFan(const uint8_t fan) {
  return fan == kBluestarFanLow || fan == kBluestarFanMed ||
      fan == kBluestarFanHigh || fan == kBluestarFanAuto;
}
uint8_t rememberedFan(const uint8_t fan) {
  if (fan == kBluestarFanTurbo) return kBluestarFanHigh;
  return validNormalFan(fan) ? fan : kBluestarFanAuto;
}
bool parseTemperature(const String &value, uint8_t *temperature) {
  if (value.length() != 2 || value[0] < '0' || value[0] > '9' ||
      value[1] < '0' || value[1] > '9') return false;
  const int number = value.toInt();
  if (number < kBluestarMinTemp || number > kBluestarMaxTemp) return false;
  *temperature = number;
  return true;
}
}

void AcController::begin(void (*before)(), void (*after)()) {
  beforeIr = before;
  afterIr = after;
  ac.begin();
  storageReady = storage.begin("ac-controller", false);
  if (!storageReady) Serial.println(F("AC flash storage unavailable."));
  loadAcState();
  loadAutoCoolSettings();
}

void AcController::sendAcPacket() {
  if (beforeIr) beforeIr();
  ac.send();
  turboTimer.sent(millis(), ac.getPower(), ac.getTurbo());
  ++commandsSent;
  if (afterIr) afterIr();
}

void AcController::restoreAutoCoolTarget() {
  if (ac.getTemp() != autoCool.target()) {
    ac.setTemp(autoCool.target());
    sendAcPacket();
  }
}

void AcController::stopAutoCool() {
  if (autoCool.enabled() && ac.getTurbo()) {
    ac.setTurbo(false);
    sendAcPacket();
    if (ac.getPower()) restoreAutoCoolTarget();
  }
  autoCool.disable();
  saveAutoCoolSettings();
}

void AcController::service(const TemperatureReading& reading, bool printEvents) {
  const uint32_t now = millis();
  if (turboTimer.expire(now)) {
    ac.setTurbo(false);
    sendAcPacket();
    if (autoCool.enabled()) restoreAutoCoolTarget();
    else saveAcState();
    if (printEvents) Serial.println(F("Turbo off: 20-minute limit; automatic Turbo paused for 5 minutes."));
  }
  const AutoCool::Action action = autoCool.update(
      now, reading.sampledAtSeconds, reading.available(),
      reading.celsius, ac.getPower(), ac.getMode() == kBluestarCool,
      ac.getTurbo(), turboTimer.cooldownSeconds(now) == 0);
  switch (action) {
    case AutoCool::Action::TurboOn:
    case AutoCool::Action::TurboOff:
      ac.setTurbo(action == AutoCool::Action::TurboOn);
      sendAcPacket();
      restoreAutoCoolTarget();
      break;
    case AutoCool::Action::PowerOff:
      ac.setPower(false);
      sendAcPacket();
      // A reboot during recovery must restore Off, never a pending restart.
      saveAcState();
      break;
    case AutoCool::Action::PowerOn:
      ac.setPower(true);
      sendAcPacket();
      // Power commands clamp targets below 24 C. Send target restoration
      // separately so the preceding IR packet retains its Power command.
      restoreAutoCoolTarget();
      saveAcState();
      break;
    default: return;
  }
  // Turbo transitions never write flash. Only recovery power changes update
  // the existing saved AC state; recovery timers/latches remain in RAM.
  if (printEvents)
    Serial.printf("[auto-cool] %s; target %u C\n",
        action == AutoCool::Action::PowerOff ? "Recovery: off for 120s" :
        action == AutoCool::Action::PowerOn ? "Recovery: restarted" :
        ac.getTurbo() ? "Turbo on" : "Turbo off", autoCool.target());
}

void AcController::loadAcState() {
  if (!storageReady) return;
  const size_t length = storage.getBytesLength("ac-state");
  if (length == 0) return;  // First boot: keep the library defaults.
  SavedAcState saved{};
  if (length != sizeof(saved) ||
      storage.getBytes("ac-state", &saved, sizeof(saved)) != sizeof(saved) ||
      saved.version != kAcStateVersion || saved.raw[0] != 0x33 ||
      saved.raw[10] > 7 || !IRBluestarAc::validChecksum(saved.raw) ||
      !validNormalFan(saved.normalFan)) {
    Serial.println(F("Saved AC state invalid or incompatible; using defaults."));
    return;
  }
  ac.setRaw(saved.raw);
  // Restore the native class's fan history as well as the current packet.
  // setFan updates that history; copying the packet back preserves its active
  // capacity override and original secondary command without transmitting.
  ac.setFan(saved.normalFan);
  std::memcpy(ac.getRaw(), saved.raw, sizeof(saved.raw));
  normalFan = saved.normalFan;
  lastSavedAcState = saved;
  haveSavedAcState = true;
  // The elapsed time of a saved session is unknown. Restore normal settings
  // without transmitting or starting a fresh Turbo session on boot.
  if (ac.getTurbo()) ac.setTurbo(false);
  Serial.println(F("Restored saved AC settings; no IR sent."));
}

bool AcController::saveAcState() {
  SavedAcState value{};
  value.version = kAcStateVersion;
  std::memcpy(value.raw, ac.getRaw(), sizeof(value.raw));
  if (autoCool.enabled()) {
    // Manual target/swing edits must not accidentally persist automatic Turbo.
    IRBluestarAc desired(irPin);
    desired.setRaw(value.raw);
    desired.setTurbo(false);
    desired.setTemp(autoCool.target());
    std::memcpy(value.raw, desired.getRaw(), sizeof(value.raw));
  }
  value.normalFan = normalFan;
  // Avoid flash writes for polling, repeated sends, and unchanged settings.
  if (haveSavedAcState && value.version == lastSavedAcState.version &&
      value.normalFan == lastSavedAcState.normalFan &&
      std::memcmp(value.raw, lastSavedAcState.raw, sizeof(value.raw)) == 0)
    return acStateSaved = true;
  // Keep the complete state in one NVS entry, independently of Wi-Fi settings.
  if (!storageReady ||
      storage.putBytes("ac-state", &value, sizeof(value)) != sizeof(value)) {
    Serial.println(F("Could not save AC settings; previous saved state retained."));
    return acStateSaved = false;
  }
  lastSavedAcState = value;
  haveSavedAcState = true;
  return acStateSaved = true;
}

bool AcController::saveAutoCoolSettings() {
  const SavedAutoCool value{1, autoCool.target(),
      static_cast<uint8_t>(autoCool.enabled()), 0};
  if (haveSavedAutoCool && std::memcmp(&value, &lastSavedAutoCool, sizeof(value)) == 0) {
    autoCoolSettingsSaved = true;
    return true;
  }
  autoCoolSettingsSaved = storageReady &&
      storage.putBytes("auto-cool", &value, sizeof(value)) == sizeof(value);
  if (!autoCoolSettingsSaved) {
    Serial.println(F("Could not save Auto-Cool settings; previous saved settings retained."));
    return false;
  }
  lastSavedAutoCool = value;
  haveSavedAutoCool = true;
  return true;
}

void AcController::loadAutoCoolSettings() {
  if (!storageReady || !storage.getBytesLength("auto-cool")) return;
  SavedAutoCool saved{};
  if (storage.getBytesLength("auto-cool") != sizeof(saved) ||
      storage.getBytes("auto-cool", &saved, sizeof(saved)) != sizeof(saved) ||
      saved.version != 1 || saved.reserved || saved.enabled > 1 ||
      saved.target < kBluestarMinTemp || saved.target > kBluestarMaxTemp) {
    Serial.println(F("Saved Auto-Cool settings invalid; Auto-Cool disabled."));
    return;
  }
  lastSavedAutoCool = saved;
  haveSavedAutoCool = true;
  autoCool.setTarget(saved.target);
  if (saved.enabled && ac.getPower() && ac.getMode() == kBluestarCool) {
    autoCool.enable(saved.target);
    ac.setTemp(saved.target);
    ac.setTurbo(false);
    Serial.println(F("Restored Auto-Cool target; waiting for a valid sensor reading. No recovery resumed."));
  }
}

bool AcController::loadState(String hex) {
  hex.replace(" ", "");
  if (hex.length() != kBluestarStateLength * 2) return false;
  uint8_t raw[kBluestarStateLength];
  for (uint8_t i = 0; i < kBluestarStateLength; i++) {
    const int high = hexDigit(hex[i * 2]);
    const int low = hexDigit(hex[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    raw[i] = (high << 4) | low;
  }
  return raw[0] == 0x33 && IRBluestarAc::validChecksum(raw) && ac.setRaw(raw);
}

bool AcController::command(String line, String *message, const TemperatureReading& reading) {
  line.trim();
  line.toLowerCase();
  const int space = line.indexOf(' ');
  const String command = space < 0 ? line : line.substring(0, space);
  String value = space < 0 ? "" : line.substring(space + 1);
  value.trim();
  bool transmit = true;
  *message = "Invalid command. Type help for available commands.";
  if (command == "autocool") {
    bool on;
    if (value == "toggle") on = !autoCool.enabled();
    else if (value == "on") on = true;
    else if (value == "off") on = false;
    else return false;
    if (on && !autoCool.enabled()) {
      if (!ac.getPower() || ac.getMode() != kBluestarCool || !reading.available()) {
        *message = "Auto-Cool requires AC on in Cool mode and a valid temperature reading.";
        return false;
      }
      if (ac.getEcono() || ac.getSleep()) {
        *message = "Turn off Energy Save and Sleep before enabling Auto-Cool.";
        return false;
      }
      autoCool.enable(ac.getTemp());
    } else if (!on) {
      const bool waiting = autoCool.waiting();
      stopAutoCool();
      *message = waiting ? "Auto-Cool disabled. Recovery cancelled; AC remains off." :
          "Auto-Cool disabled.";
      if (!autoCoolSettingsSaved) *message += " Warning: Auto-Cool settings could not be saved.";
      return true;
    }
    *message = "Auto-Cool enabled.";
    if (!saveAcState()) *message += " Warning: AC settings could not be saved.";
    if (!saveAutoCoolSettings()) *message += " Warning: Auto-Cool settings could not be saved.";
    return true;
  }
  // A queued automatic restart must never override an explicit manual Off.
  // Prevent early restart and settings changes during the two-minute wait.
  if (autoCool.waiting() && command != "off" && command != "state") {
    *message = "Recovery cooldown active. Use off or autocool off to cancel; AC remains off.";
    return false;
  }
  if ((command == "on" || command == "off") && value.length() == 0) {
    stopAutoCool();
    ac.setPower(command == "on");
  } else if (command == "power" && value == "toggle") {
    stopAutoCool();
    ac.setPower(!ac.getPower());
  } else if (command == "temp") {
    if (ac.getMode() == kBluestarFanOnly) {
      *message = "Fan-only mode has no temperature setting.";
      return false;
    }
    uint8_t temp = autoCool.enabled() ? autoCool.target() : ac.getTemp();
    if (value == "up") {
      temp = temp < kBluestarMaxTemp ? temp + 1 : temp;
    } else if (value == "down") {
      temp = temp > kBluestarMinTemp ? temp - 1 : temp;
    } else if (!parseTemperature(value, &temp)) {
      *message = "Temperature must be 16..30 C, up, or down.";
      return false;
    }
    ac.setTemp(temp);
    autoCool.setTarget(temp);
  } else if (command == "mode") {
    uint8_t mode;
    if (value == "auto") {
      mode = kBluestarAuto;
    } else if (value == "cool") {
      mode = kBluestarCool;
    } else if (value == "dry" || value == "dehumid") {
      mode = kBluestarDry;
    } else if (value == "fan" || value == "fan-only") {
      mode = kBluestarFanOnly;
    } else if (value == "next") {
      switch (ac.getMode()) {
        case kBluestarAuto: mode = kBluestarCool; break;
        case kBluestarCool: mode = kBluestarDry; break;
        case kBluestarDry: mode = kBluestarFanOnly; break;
        default: mode = kBluestarAuto;
      }
    } else {
      return false;
    }
    stopAutoCool();
    ac.setMode(mode);
  } else if (command == "fan") {
    uint8_t fan;
    if (value == "auto") {
      fan = kBluestarFanAuto;
    } else if (value == "low") {
      fan = kBluestarFanLow;
    } else if (value == "medium") {
      fan = kBluestarFanMed;
    } else if (value == "high") {
      fan = kBluestarFanHigh;
    } else if (value == "next") {
      switch (ac.getFan()) {
        case kBluestarFanLow: fan = kBluestarFanMed; break;
        case kBluestarFanMed: fan = kBluestarFanHigh; break;
        case kBluestarFanHigh:
        case kBluestarFanTurbo: fan = kBluestarFanAuto; break;
        default: fan = kBluestarFanLow;
      }
    } else {
      return false;
    }
    stopAutoCool();
    ac.setFan(fan);
    normalFan = fan;
  } else if (command == "capacity") {
    uint8_t capacity;
    if (value == "100") {
      capacity = kBluestarCapacityDefault;
    } else if (value == "110") {
      capacity = kBluestarCapacityTurbo;
    } else if (value == "80") {
      capacity = kBluestarCapacity80;
    } else if (value == "60") {
      capacity = kBluestarCapacity60;
    } else if (value == "40") {
      capacity = kBluestarCapacity40;
    } else if (value == "next") {
      capacity = ac.getCapacity() >= kBluestarCapacityDefault ?
          kBluestarCapacityTurbo : ac.getCapacity() + 1;
    } else {
      return false;
    }
    stopAutoCool();
    if (ac.getCapacity() == kBluestarCapacityDefault)
      normalFan = rememberedFan(ac.getFan());
    ac.setCapacity(capacity);
  } else if (command == "horizontal" || command == "vertical" ||
             command == "light" || command == "turbo" ||
             command == "econo" || command == "sleep") {
    bool current;
    if (command == "horizontal") current = ac.getSwingH();
    else if (command == "vertical") current = ac.getSwingV();
    else if (command == "light") current = ac.getLight();
    else if (command == "turbo") current = ac.getTurbo();
    else if (command == "econo") current = ac.getEcono();
    else current = ac.getSleep();
    bool on;
    if (value == "toggle") on = !current;
    else if (value == "on") on = true;
    else if (value == "off") on = false;
    else return false;
    if (command == "turbo" || command == "econo" || command == "sleep")
      stopAutoCool();
    if (command == "horizontal") ac.setSwingH(on);
    else if (command == "vertical") ac.setSwingV(on);
    else if (command == "light") ac.setLight(on);
    else if (command == "turbo") ac.setTurbo(on);
    else if (command == "econo") ac.setEcono(on);
    else ac.setSleep(on);
    if (command == "econo" && on) normalFan = kBluestarFanLow;
  } else if (command == "load") {
    if (!loadState(value)) {
      *message = "Invalid state length, hexadecimal value, or checksum.";
      return false;
    }
    normalFan = rememberedFan(ac.getFan());
    autoCool.disable();
    turboTimer.sent(millis(), false, false);  // Imported state is inert until sent.
    transmit = false;
  } else if ((command == "state" || command == "send") && !value.length()) {
    transmit = command == "send";
  } else {
    return false;
  }
  if (transmit) {
    // Preserve the software IR carrier's original CPU timing during sends.
    sendAcPacket();
    *message = "Command sent.";
  } else {
    *message = command == "load" ? "State loaded; use send to transmit." :
        "Current controller settings.";
  }
  if ((transmit || command == "load") && !saveAcState())
    *message += " Warning: AC settings could not be saved.";
  if (command != "state" && !saveAutoCoolSettings())
    *message += " Warning: Auto-Cool settings could not be saved.";
  return true;
}

void AcController::printState() {
  Serial.println(ac.toString());
  Serial.print(F("State: "));
  const uint8_t *raw = ac.getRaw();
  for (uint8_t i = 0; i < kBluestarStateLength; i++) {
    if (raw[i] < 16) Serial.print('0');
    Serial.print(raw[i], HEX);
    if (i + 1 < kBluestarStateLength) Serial.print(' ');
  }
  Serial.println();
}

void AcController::appendJson(String& result) {
  result += ",\"power\":" + String(ac.getPower() ? "true" : "false");
  result += ",\"mode\":" + String(ac.getMode());
  result += ",\"temperature\":" + String(ac.getTemp());
  result += ",\"autoCool\":{\"enabled\":" + String(autoCool.enabled() ? "true" : "false");
  result += ",\"target\":" + String(autoCool.target());
  result += ",\"waiting\":" + String(autoCool.waiting() ? "true" : "false");
  result += ",\"waitSeconds\":" + String(autoCool.waitSeconds(millis()));
  result += ",\"recoveryUsed\":" + String(autoCool.recoveryUsed() ? "true" : "false");
  result += ",\"cancelled\":" + String(autoCool.cancelled() ? "true" : "false");
  result += ",\"saved\":" + String((autoCoolSettingsSaved && acStateSaved) ? "true" : "false") + "}";
  result += ",\"fan\":" + String(ac.getFan());
  result += ",\"capacity\":" + String(ac.getCapacity());
  result += ",\"horizontal\":" + String(ac.getSwingH() ? "true" : "false");
  result += ",\"vertical\":" + String(ac.getSwingV() ? "true" : "false");
  result += ",\"light\":" + String(ac.getLight() ? "true" : "false");
  result += ",\"turbo\":" + String(ac.getTurbo() ? "true" : "false");
  result += ",\"turboCooldownSeconds\":" + String(turboTimer.cooldownSeconds(millis()));
  result += ",\"econo\":" + String(ac.getEcono() ? "true" : "false");
  result += ",\"sleep\":" + String(ac.getSleep() ? "true" : "false");
  result += ",\"sent\":" + String(commandsSent);
}
