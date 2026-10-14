// Room Controller: AC infrared, temperature sensing, and BLE lighting.
// No IR commands are transmitted at boot or when restoring Wi-Fi settings.

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <IRremoteESP8266.h>
#include <ir_Bluestar.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <driver/uart.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "web_ui.h"
#include "lotus_lamp.h"
#include "temperature_history.h"
#include "auto_cool.h"

const uint16_t kIrLed = 23;
const uint8_t kTemperaturePin = 22;
const uint8_t kActivityLedPin = 2;  // NodeMCU-32S blue user LED.
const uint8_t kActivityLedOnLevel = HIGH;  // Reference board is active-high.
const uint32_t kTemperatureInterval = TemperatureHistory::kIntervalSeconds * 1000;
const uint32_t kTemperatureConversionTime = 750;  // DS18B20 at 12-bit resolution.
const uint32_t kSettingsVersion = 1;
const uint32_t kAcStateVersion = 1;
const uint32_t kConnectTimeout = 15000;
const uint32_t kReconnectInterval = 30000;

#if !CONFIG_PM_ENABLE || !CONFIG_FREERTOS_USE_TICKLESS_IDLE
#error "Automatic light sleep requires the Arduino + ESP-IDF build and sdkconfig.defaults"
#endif

struct NetworkSettings {
  uint32_t version;
  char ssid[33];
  char password[65];
  char hostname[64];
};

struct SavedAcState {
  uint32_t version;
  uint8_t raw[kBluestarStateLength];
  uint8_t normalFan;  // Fan to restore when returning to 100% capacity.
};

struct SavedAutoCool {
  uint32_t version;
  uint8_t target;
  uint8_t enabled;
  uint16_t reserved;
};
static_assert(sizeof(SavedAutoCool) == 8, "Unexpected Auto-Cool settings size");

IRBluestarAc ac(kIrLed);
LotusLampController lamp;
WebServer server(80);
Preferences storage;
NetworkSettings settings{};
bool storageReady = false;
bool networkUp = false;
bool connecting = false;
uint32_t connectionStarted = 0;
uint32_t commandsSent = 0;
uint32_t lastUserActivity = 0;
bool cpuBoosted = false;
bool powerManagementReady = false;
esp_pm_lock_handle_t cpuLock = nullptr, awakeLock = nullptr;
uint32_t lastSerialActivity = 0;
bool haveSerialActivity = false;
SavedAcState lastSavedAcState{};
bool haveSavedAcState = false;
uint8_t normalFan = kBluestarFanLow;

enum class SetupStep { None, Ssid, Password };
SetupStep setupStep = SetupStep::None;
String pendingSsid;

OneWire temperatureBus(kTemperaturePin);
DallasTemperature temperatureSensor(&temperatureBus);
DeviceAddress temperatureAddress{};
enum class SensorStatus { Waiting, Ready, Unavailable };
SensorStatus sensorStatus = SensorStatus::Waiting;
bool sensorPresent = false;
bool sensorConverting = false;
uint32_t temperatureStarted = 0;
float measuredTemperature = 0;
uint32_t measuredTemperatureSeconds = 0;
TemperatureHistory temperatureHistory;
AutoCool autoCool;
SavedAutoCool lastSavedAutoCool{};
bool haveSavedAutoCool = false;
bool autoCoolSettingsSaved = true;
bool saveAutoCoolSettings();
bool saveAcState();

void acquireActivePower() {
  if (powerManagementReady && !cpuBoosted) {
    esp_pm_lock_acquire(cpuLock);
    esp_pm_lock_acquire(awakeLock);
    cpuBoosted = true;
    digitalWrite(kActivityLedPin, kActivityLedOnLevel);
  }
}

void boostCpu() {
  lastUserActivity = millis();
  acquireActivePower();
}

void beginPowerManagement() {
  // Locks preserve 240 MHz and prevent sleep during IR, BLE, and setup. Keep
  // APB at 80 MHz even at the minimum CPU speed for UART and OneWire timing.
  esp_err_t error = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "controls", &cpuLock);
  if (error == ESP_OK)
    error = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "controls", &awakeLock);
  if (error == ESP_OK) error = uart_set_wakeup_threshold(UART_NUM_0, 3);
  if (error == ESP_OK) error = esp_sleep_enable_uart_wakeup(UART_NUM_0);
  const esp_pm_config_esp32_t config = {240, 80, true};
  if (error == ESP_OK) error = esp_pm_configure(&config);
  if (error != ESP_OK) {
    // SDK startup frequency is 240 MHz. Leave it alone if PM cannot start.
    if (cpuLock) { esp_pm_lock_delete(cpuLock); cpuLock = nullptr; }
    if (awakeLock) { esp_pm_lock_delete(awakeLock); awakeLock = nullptr; }
    Serial.printf("[power] Automatic light sleep unavailable: %s\n", esp_err_to_name(error));
    return;
  }
  powerManagementReady = true;
  boostCpu();
  Serial.println(F("[power] Automatic light sleep; CPU 80..240 MHz; Wi-Fi MIN_MODEM"));
  Serial.println(F("[power] Press Enter before serial commands after idle; serial stays awake for 60s."));
}

void serviceCpuPower() {
  if (!powerManagementReady) return;
  if (setupStep != SetupStep::None || connecting || lamp.needsFastCpu() ||
      (haveSerialActivity && millis() - lastSerialActivity < 60000)) {
    acquireActivePower();
  } else if (cpuBoosted && millis() - lastUserActivity >= 5000) {
    // The IDF scheduler/drivers now manage DFS and automatic light sleep;
    // never manually change CPU clocks behind their power locks.
    esp_pm_lock_release(awakeLock);
    esp_pm_lock_release(cpuLock);
    cpuBoosted = false;
    digitalWrite(kActivityLedPin, !kActivityLedOnLevel);
  }
}

const char *sensorStatusName() {
  switch (sensorStatus) {
    case SensorStatus::Ready: return "ready";
    case SensorStatus::Unavailable: return "unavailable";
    default: return "waiting";
  }
}

void printTemperature() {
  Serial.print(F("Measured temperature (GPIO 22): "));
  if (sensorStatus == SensorStatus::Ready) {
    Serial.print(measuredTemperature, 2);
    Serial.println(F(" C"));
  } else if (sensorStatus == SensorStatus::Waiting) {
    Serial.println(F("waiting for first reading"));
  } else {
    Serial.println(F("sensor unavailable"));
  }
}

void startTemperatureReading() {
  if (!sensorPresent) {
    // Retry discovery after a missing/disconnected sensor without rebooting.
    temperatureSensor.begin();
    for (uint8_t i = 0; i < temperatureSensor.getDeviceCount(); i++) {
      if (temperatureSensor.getAddress(temperatureAddress, i) &&
          temperatureAddress[0] == 0x28) {  // DS18B20 family code.
        sensorPresent = temperatureSensor.setResolution(temperatureAddress, 12);
        if (sensorPresent) break;
      }
    }
  }
  sensorConverting = sensorPresent &&
      temperatureSensor.requestTemperaturesByAddress(temperatureAddress);
  temperatureStarted = millis();
  if (!sensorConverting) {
    sensorPresent = false;
    sensorStatus = SensorStatus::Unavailable;
  } else if (sensorStatus != SensorStatus::Ready) {
    sensorStatus = SensorStatus::Waiting;
  }
}

void beginTemperature() {
  temperatureSensor.setWaitForConversion(false);
  temperatureSensor.setAutoSaveScratchPad(false);
  startTemperatureReading();
}

void serviceTemperature() {
  const uint32_t elapsed = millis() - temperatureStarted;
  const SensorStatus previousStatus = sensorStatus;
  if (sensorConverting) {
    if (elapsed < kTemperatureConversionTime) return;
    sensorConverting = false;
    const float reading = temperatureSensor.getTempC(temperatureAddress);
    if (std::isfinite(reading) && reading >= -55 && reading <= 125) {
      measuredTemperature = reading;
      measuredTemperatureSeconds = esp_timer_get_time() / 1000000;
      sensorStatus = SensorStatus::Ready;
    } else {
      sensorPresent = false;
      sensorStatus = SensorStatus::Unavailable;
    }
    // Do not interrupt SSID/password prompts with periodic sensor output.
    if (setupStep == SetupStep::None &&
        (sensorStatus == SensorStatus::Ready || sensorStatus != previousStatus))
      printTemperature();
  } else if (elapsed >= kTemperatureInterval) {
    startTemperatureReading();
    if (setupStep == SetupStep::None && sensorStatus == SensorStatus::Unavailable &&
        sensorStatus != previousStatus) printTemperature();
  }
}

bool temperatureAvailable() {
  const uint32_t now = esp_timer_get_time() / 1000000;
  return sensorStatus == SensorStatus::Ready &&
      now - measuredTemperatureSeconds <= 7;
}

void sendAcPacket() {
  boostCpu();
  ac.send();
  commandsSent++;
}

void restoreAutoCoolTarget() {
  if (ac.getTemp() != autoCool.target()) {
    ac.setTemp(autoCool.target());
    sendAcPacket();
  }
}

void stopAutoCool() {
  if (autoCool.enabled() && ac.getTurbo()) {
    ac.setTurbo(false);
    sendAcPacket();
    if (ac.getPower()) restoreAutoCoolTarget();
  }
  autoCool.disable();
  saveAutoCoolSettings();
}

void serviceAutoCool() {
  const AutoCool::Action action = autoCool.update(
      millis(), measuredTemperatureSeconds, temperatureAvailable(),
      measuredTemperature, ac.getPower(), ac.getMode() == kBluestarCool,
      ac.getTurbo());
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
      if (!saveAcState()) autoCoolSettingsSaved = false;
      break;
    case AutoCool::Action::PowerOn:
      ac.setPower(true);
      sendAcPacket();
      // Power commands clamp targets below 24 C. Send target restoration
      // separately so the preceding IR packet retains its Power command.
      restoreAutoCoolTarget();
      if (!saveAcState()) autoCoolSettingsSaved = false;
      break;
    default: return;
  }
  // Turbo transitions never write flash. Only recovery power changes update
  // the existing saved AC state; recovery timers/latches remain in RAM.
  if (setupStep == SetupStep::None)
    Serial.printf("[auto-cool] %s; target %u C\n",
        action == AutoCool::Action::PowerOff ? "Recovery: off for 120s" :
        action == AutoCool::Action::PowerOn ? "Recovery: restarted" :
        ac.getTurbo() ? "Turbo on" : "Turbo off", autoCool.target());
}

bool validHostname(const String &name) {
  if (name.length() == 0 || name.length() > 63 || name[0] == '-' ||
      name[name.length() - 1] == '-') return false;
  for (size_t i = 0; i < name.length(); i++) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
      return false;
  }
  return true;
}

int hexDigit(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool validPassword(const String &password) {
  if (password.length() == 0) return true;  // Open network.
  if (password.length() >= 8 && password.length() <= 63) return true;
  if (password.length() != 64) return false;
  for (size_t i = 0; i < password.length(); i++)
    if (hexDigit(password[i]) < 0) return false;
  return true;  // A 64-character hexadecimal WPA key.
}

bool validSettings(const NetworkSettings &value) {
  return value.version == kSettingsVersion &&
      std::memchr(value.ssid, '\0', sizeof(value.ssid)) &&
      std::memchr(value.password, '\0', sizeof(value.password)) &&
      std::memchr(value.hostname, '\0', sizeof(value.hostname)) &&
      validHostname(String(value.hostname)) &&
      validPassword(String(value.password));
}

void loadSettings() {
  settings.version = kSettingsVersion;
  snprintf(settings.hostname, sizeof(settings.hostname), "esp32");
  // Keep the legacy NVS namespace so existing settings survive the rename.
  storageReady = storage.begin("ac-controller", false);
  if (!storageReady) {
    Serial.println(F("Flash storage unavailable; Wi-Fi and AC settings cannot save."));
    return;
  }
  NetworkSettings saved{};
  if (storage.getBytesLength("network") == sizeof(saved) &&
      storage.getBytes("network", &saved, sizeof(saved)) == sizeof(saved) &&
      validSettings(saved)) settings = saved;
}

bool saveSettings(const NetworkSettings &value) {
  // A single NVS entry keeps the SSID/password pair together across updates.
  if (!storageReady ||
      storage.putBytes("network", &value, sizeof(value)) != sizeof(value)) {
    Serial.println(F("Could not save settings; previous settings retained."));
    return false;
  }
  settings = value;
  return true;
}

bool validNormalFan(const uint8_t fan) {
  return fan == kBluestarFanLow || fan == kBluestarFanMed ||
      fan == kBluestarFanHigh || fan == kBluestarFanAuto;
}

uint8_t rememberedFan(const uint8_t fan) {
  if (fan == kBluestarFanTurbo) return kBluestarFanHigh;
  return validNormalFan(fan) ? fan : kBluestarFanAuto;
}

void loadAcState() {
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
  Serial.println(F("Restored saved AC settings; no IR sent."));
}

bool saveAcState() {
  SavedAcState value{};
  value.version = kAcStateVersion;
  std::memcpy(value.raw, ac.getRaw(), sizeof(value.raw));
  if (autoCool.enabled()) {
    // Manual target/swing edits must not accidentally persist automatic Turbo.
    IRBluestarAc desired(kIrLed);
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
    return true;
  // Keep the complete state in one NVS entry, independently of Wi-Fi settings.
  if (!storageReady ||
      storage.putBytes("ac-state", &value, sizeof(value)) != sizeof(value)) {
    Serial.println(F("Could not save AC settings; previous saved state retained."));
    return false;
  }
  lastSavedAcState = value;
  haveSavedAcState = true;
  return true;
}

bool saveAutoCoolSettings() {
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

void loadAutoCoolSettings() {
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

void stopNetworkServices() {
  server.stop();
  MDNS.end();
  networkUp = false;
}

void connectWifi() {
  boostCpu();
  stopNetworkServices();
  WiFi.disconnect(true, false);
  connecting = false;
  if (!settings.ssid[0]) return;
  WiFi.persistent(false);  // Credentials are managed in our NVS entry.
  WiFi.setHostname(settings.hostname);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(settings.ssid, settings.password);
  // Sleep between DTIM beacons while preserving multicast/mDNS reception.
  const esp_err_t error = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  if (error != ESP_OK)
    Serial.printf("Wi-Fi modem sleep setup failed: %s\n", esp_err_to_name(error));
  connectionStarted = millis();
  connecting = true;
  Serial.print(F("Connecting to Wi-Fi: "));
  Serial.println(settings.ssid);
}

void printNetworkStatus() {
  Serial.print(F("Hostname: "));
  Serial.print(settings.hostname);
  Serial.println(F(".local"));
  if (!settings.ssid[0]) {
    Serial.println(F("No saved Wi-Fi. Use 'wifi setup'."));
  } else {
    Serial.print(F("SSID: "));
    Serial.println(settings.ssid);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(F("Web remote: http://"));
      Serial.print(settings.hostname);
      Serial.println(F(".local/"));
      Serial.print(F("IP address: http://"));
      Serial.print(WiFi.localIP());
      Serial.println('/');
    } else {
      Serial.println(F("Wi-Fi disconnected; saved credentials will retry."));
    }
  }
}

void beginWifiSetup() {
  if (setupStep != SetupStep::None) return;
  pendingSsid = "";
  setupStep = SetupStep::Ssid;
  Serial.println(F("Wi-Fi setup. Type /cancel to leave setup."));
  Serial.println(F("Enter SSID (case-sensitive), then press Enter:"));
}

void handleSetupInput(const String &line) {
  if (line == "/cancel") {
    setupStep = SetupStep::None;
    pendingSsid = "";
    Serial.println(F("Wi-Fi setup cancelled; existing settings retained."));
    return;
  }
  if (setupStep == SetupStep::Ssid) {
    // Ignore empty input without repeatedly printing the setup prompt.
    if (line.length() == 0) return;
    if (line.length() > 32) {
      Serial.println(F("SSID must contain 1..32 bytes. Enter SSID:"));
      return;
    }
    pendingSsid = line;
    setupStep = SetupStep::Password;
    Serial.println(F("Enter Wi-Fi password (case-sensitive), then Enter."));
    Serial.println(F("Firmware does not echo it. Empty = open network:"));
    return;
  }
  if (!validPassword(line)) {
    Serial.println(F("Use 8..63 bytes, a 64-digit hex key, or an empty line."));
    Serial.println(F("Enter password again, or /cancel:"));
    return;
  }
  NetworkSettings updated = settings;
  std::memset(updated.ssid, 0, sizeof(updated.ssid));
  std::memset(updated.password, 0, sizeof(updated.password));
  pendingSsid.toCharArray(updated.ssid, sizeof(updated.ssid));
  line.toCharArray(updated.password, sizeof(updated.password));
  if (!saveSettings(updated)) return;
  pendingSsid = "";
  setupStep = SetupStep::None;
  Serial.println(F("Wi-Fi credentials saved across power loss."));
  connectWifi();
}

void printHelp() {
  Serial.print(F("Room Controller, IR GPIO "));
  Serial.print(kIrLed);
  Serial.print(F(", temperature GPIO "));
  Serial.print(kTemperaturePin);
  Serial.println(F(", 115200 8-N-1. Commands:"));
  Serial.println(F("  on / off / power toggle / temp 16..30|up|down"));
  Serial.println(F("  mode auto|cool|dry|fan|next"));
  Serial.println(F("  fan auto|low|medium|high|next"));
  Serial.println(F("  capacity 100|110|80|60|40|next"));
  Serial.println(F("  horizontal|vertical|light|turbo|econo|sleep"));
  Serial.println(F("    on|off|toggle"));
  Serial.println(F("  state / send / load <24 hexadecimal digits> / help"));
  Serial.println(F("  sensor   (measured temperature on GPIO 22; updates every 5s)"));
  Serial.println(F("  autocool on|off|toggle (AC on in Cool mode; valid sensor required)"));
  Serial.println(F("  wifi setup / wifi status / wifi reconnect"));
  Serial.println(F("  wifi forget"));
  Serial.println(F("  hostname [new-name]   (without .local; saved in flash)"));
  Serial.println(F("  lamp scan / lamp pair <number> / lamp status / lamp forget"));
  Serial.println(F("  lamp connect / lamp reconnect / lamp disconnect / lamp on / lamp off"));
  Serial.println(F("  lamp color <name|#RRGGBB>"));
  Serial.println(F("  lamp brightness 0..100 / lamp effect 0..212 / lamp speed 0..100"));
}

void printState() {
  Serial.println(ac.toString());
  Serial.print(F("State: "));
  const uint8_t *raw = ac.getRaw();
  for (uint8_t i = 0; i < kBluestarStateLength; i++) {
    if (raw[i] < 16) Serial.print('0');
    Serial.print(raw[i], HEX);
    if (i + 1 < kBluestarStateLength) Serial.print(' ');
  }
  Serial.println();
  printTemperature();
}

bool loadState(String hex) {
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

bool parseTemperature(const String &value, uint8_t *temperature) {
  if (value.length() != 2 || value[0] < '0' || value[0] > '9' ||
      value[1] < '0' || value[1] > '9') return false;
  const int number = value.toInt();
  if (number < kBluestarMinTemp || number > kBluestarMaxTemp) return false;
  *temperature = number;
  return true;
}

// Serial and web share this handler. Only validated actions send IR.
bool executeAcCommand(String line, String *message) {
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
      if (!ac.getPower() || ac.getMode() != kBluestarCool || !temperatureAvailable()) {
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

String jsonString(const String &value) {
  String result = "\"";
  for (size_t i = 0; i < value.length(); i++) {
    const uint8_t c = value[i];
    if (c == '"' || c == '\\') {
      result += '\\';
      result += static_cast<char>(c);
    } else if (c < 32) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      result += escaped;
    } else {
      result += static_cast<char>(c);
    }
  }
  return result + '"';
}

String stateJson(const String &message = "", const bool ok = true) {
  String result;
  result.reserve(1100);
  result = "{\"ok\":";
  result += ok ? "true" : "false";
  result += ",\"message\":" + jsonString(message);
  result += ",\"hostname\":" + jsonString(String(settings.hostname));
  result += ",\"ip\":" + jsonString(WiFi.localIP().toString());
  result += ",\"power\":" + String(ac.getPower() ? "true" : "false");
  result += ",\"mode\":" + String(ac.getMode());
  result += ",\"temperature\":" + String(ac.getTemp());
  result += ",\"autoCool\":{\"enabled\":" + String(autoCool.enabled() ? "true" : "false");
  result += ",\"target\":" + String(autoCool.target());
  result += ",\"waiting\":" + String(autoCool.waiting() ? "true" : "false");
  result += ",\"waitSeconds\":" + String(autoCool.waitSeconds(millis()));
  result += ",\"recoveryUsed\":" + String(autoCool.recoveryUsed() ? "true" : "false");
  result += ",\"cancelled\":" + String(autoCool.cancelled() ? "true" : "false");
  result += ",\"saved\":" + String(autoCoolSettingsSaved ? "true" : "false") + "}";
  result += ",\"measuredTemperature\":";
  result += sensorStatus == SensorStatus::Ready ?
      String(measuredTemperature, 2) : String("null");
  result += ",\"sensorStatus\":" + jsonString(String(sensorStatusName()));
  result += ",\"sensorIntervalSeconds\":" + String(kTemperatureInterval / 1000);
  result += ",\"fan\":" + String(ac.getFan());
  result += ",\"capacity\":" + String(ac.getCapacity());
  result += ",\"horizontal\":" + String(ac.getSwingH() ? "true" : "false");
  result += ",\"vertical\":" + String(ac.getSwingV() ? "true" : "false");
  result += ",\"light\":" + String(ac.getLight() ? "true" : "false");
  result += ",\"turbo\":" + String(ac.getTurbo() ? "true" : "false");
  result += ",\"econo\":" + String(ac.getEcono() ? "true" : "false");
  result += ",\"sleep\":" + String(ac.getSleep() ? "true" : "false");
  result += ",\"sent\":" + String(commandsSent);
  result += ",\"lamp\":" + lamp.json() + "}";
  return result;
}

void sendTemperatureHistory() {
  boostCpu();
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  char chunk[256];
  size_t used = snprintf(chunk, sizeof(chunk),
      "{\"interval\":%lu,\"age\":%lu,\"samples\":[",
      static_cast<unsigned long>(TemperatureHistory::kIntervalSeconds),
      static_cast<unsigned long>(temperatureHistory.age(esp_timer_get_time() / 1000000)));
  for (uint16_t i = 0; i < temperatureHistory.count(); i++) {
    // A sample needs at most 8 characters plus its comma. Flush bounded chunks.
    if (used + 16 >= sizeof(chunk)) {
      server.sendContent(chunk, used);
      used = 0;
    }
    const int16_t value = temperatureHistory.sample(i);
    if (value == TemperatureHistory::kMissing)
      used += snprintf(chunk + used, sizeof(chunk) - used, "%snull", i ? "," : "");
    else
      used += snprintf(chunk + used, sizeof(chunk) - used, "%s%.2f", i ? "," : "", value / 100.0);
  }
  server.sendContent(chunk, used);
  server.sendContent("]}");
  server.sendContent("");
}

void configureWebServer() {
  const char *headers[] = {"Origin"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, []() {
    boostCpu();
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", kControlPage);
  });
  server.on("/api/state", HTTP_GET, []() {
    boostCpu();
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", stateJson());
  });
  server.on("/api/temperature/history", HTTP_GET, sendTemperatureHistory);
  server.on("/api/command", HTTP_POST, []() {
    boostCpu();
    server.sendHeader("Cache-Control", "no-store");
    const String origin = server.header("Origin");
    if (origin.length() && origin != "http://" + server.hostHeader()) {
      server.send(403, "application/json",
                  stateJson("Use the controller page.", false));
      return;
    }
    const String command = server.arg("command");
    if (command.length() == 0 || command.length() > 127) {
      server.send(400, "application/json",
                  stateJson("Invalid command.", false));
      return;
    }
    String message;
    const bool ok = executeAcCommand(command, &message);
    server.send(ok ? 200 : 400, "application/json", stateJson(message, ok));
    if (ok) {
      Serial.print(F("Web: "));
      Serial.println(command);
      printState();
    }
  });
  server.on("/api/lamp/command", HTTP_POST, []() {
    boostCpu();
    server.sendHeader("Cache-Control", "no-store");
    const String origin = server.header("Origin");
    if (origin.length() && origin != "http://" + server.hostHeader()) {
      server.send(403, "application/json", stateJson("Use the controller page.", false));
      return;
    }
    const String command = server.arg("command");
    String message;
    const bool ok = command.length() > 0 && command.length() <= 100 &&
        lamp.command(command, false, message);
    if (!message.length()) message = "Invalid lamp command.";
    server.send(ok ? 202 : 400, "application/json", stateJson(message, ok));
  });
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
}

void serviceNetwork() {
  if (!settings.ssid[0]) return;
  if (WiFi.status() == WL_CONNECTED) {
    connecting = false;
    if (!networkUp) {
      networkUp = true;
      server.begin();
      if (MDNS.begin(settings.hostname)) MDNS.addService("http", "tcp", 80);
      else Serial.println(F("mDNS unavailable; use the printed IP address."));
      printNetworkStatus();
    }
    server.handleClient();
    return;
  }
  if (networkUp) {
    stopNetworkServices();
    Serial.println(F("Wi-Fi lost. Reconnecting; serial controls still work."));
    connectionStarted = millis();
    connecting = true;
  }
  const uint32_t elapsed = millis() - connectionStarted;
  if (connecting && elapsed >= kConnectTimeout) {
    connecting = false;
    Serial.println(F("Wi-Fi not connected. Try wifi setup."));
  }
  if (elapsed >= kReconnectInterval) {
    boostCpu();
    WiFi.reconnect();
    connectionStarted = millis();
    connecting = true;
    Serial.println(F("Retrying saved Wi-Fi."));
  }
}

void processSerialCommand(String line) {
  boostCpu();
  if (setupStep != SetupStep::None) {
    handleSetupInput(line);  // Preserve case and spaces in credentials.
    return;
  }
  line.trim();
  if (line.length() == 0) return;
  const int space = line.indexOf(' ');
  String command = space < 0 ? line : line.substring(0, space);
  String value = space < 0 ? "" : line.substring(space + 1);
  command.toLowerCase();
  value.trim();
  if (command == "wifi") {
    value.toLowerCase();
    if (value == "setup") {
      beginWifiSetup();
    } else if (value == "status" || value.length() == 0) {
      printNetworkStatus();
    } else if (value == "reconnect") {
      connectWifi();
    } else if (value == "forget") {
      NetworkSettings updated = settings;
      std::memset(updated.ssid, 0, sizeof(updated.ssid));
      std::memset(updated.password, 0, sizeof(updated.password));
      if (saveSettings(updated)) {
        connectWifi();  // Disconnect and stop serving.
        Serial.println(F("Saved Wi-Fi credentials forgotten."));
        beginWifiSetup();
      }
    } else {
      printHelp();
    }
    return;
  }
  if (command == "hostname") {
    if (!value.length()) {
      printNetworkStatus();
      return;
    }
    value.toLowerCase();
    if (!validHostname(value)) {
      Serial.println(F("Use 1..63 letters/digits/hyphens, without .local."));
      Serial.println(F("The first and last character cannot be a hyphen."));
      return;
    }
    NetworkSettings updated = settings;
    value.toCharArray(updated.hostname, sizeof(updated.hostname));
    if (saveSettings(updated)) {
      Serial.println(F("Hostname saved. Reconnecting with the new name."));
      connectWifi();
    }
    return;
  }
  if (command == "help" && !value.length()) {
    printHelp();
    return;
  }
  if (command == "sensor" && !value.length()) {
    printTemperature();
    return;
  }
  if (command == "lamp") {
    value.toLowerCase();
    String message;
    lamp.command(value, true, message);
    Serial.println(message);
    if (!value.length() || value == "status") lamp.printStatus();
    return;
  }
  String message;
  if (executeAcCommand(line, &message)) {
    Serial.println(message);
    printState();
  } else {
    Serial.println(message);
  }
}

void readSerial() {
  static char line[192];
  static size_t length = 0;
  static bool overflow = false;
  static bool previousCr = false;
  static bool uartWakeHandled = false;
  const bool uartWake = powerManagementReady &&
      esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UART;
  if ((uartWake && !uartWakeHandled) || Serial.available()) {
    const bool serialWasIdle = !haveSerialActivity || millis() - lastSerialActivity >= 60000;
    lastSerialActivity = millis();
    haveSerialActivity = true;
    boostCpu();
    if (uartWake && !uartWakeHandled) {
      // UART wake-up loses characters. Discard the wake input so a truncated
      // "lamp off", for example, cannot be interpreted as an AC "off" command.
      // The serial monitor sends a complete line at 115200; let it finish first.
      delay(20);
      uart_flush_input(UART_NUM_0);
      std::memset(line, 0, sizeof(line));
      length = 0;
      overflow = previousCr = false;
    }
    if (powerManagementReady && setupStep == SetupStep::None &&
        (serialWasIdle || (uartWake && !uartWakeHandled)))
      Serial.println(F("Serial awake. Enter your command."));
  }
  uartWakeHandled = uartWake;
  // A noisy or continuously active serial port must not starve the main loop.
  size_t processed = 0;
  while (Serial.available() && processed++ < 64) {
    const uint8_t c = Serial.read();
    if (c == '\n' && previousCr) {
      previousCr = false;  // CRLF is one line, including password prompts.
      continue;
    }
    previousCr = c == '\r';
    if (c == '\r' || c == '\n') {
      if (overflow) {
        Serial.println(F("Command too long; discarded."));
      } else if (length || setupStep != SetupStep::None) {
        line[length] = '\0';
        processSerialCommand(String(line));
      }
      std::memset(line, 0, sizeof(line));
      length = 0;
      overflow = false;
    } else if (!overflow && (c == '\b' || c == 127)) {
      if (length) length--;
    } else if (!overflow && c >= ' ') {
      if (length + 1 < sizeof(line)) line[length++] = c;
      else overflow = true;
    }
  }
}

const char *resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on/reset pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "exception/panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "other";
  }
}

void setup() {
  // Boot is active work at the SDK's 240 MHz startup frequency. Initialize the
  // output level before enabling it to avoid an unwanted LED glitch.
  digitalWrite(kActivityLedPin, kActivityLedOnLevel);
  pinMode(kActivityLedPin, OUTPUT);
  Serial.begin(115200, SERIAL_8N1);
  // Let the USB/serial monitor settle before printing startup text.
  delay(300);
  const esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("\n[boot] reset=%s (%d)\n", resetReasonName(reason),
                static_cast<int>(reason));
  beginPowerManagement();
  ac.begin();
  beginTemperature();
  loadSettings();
  loadAcState();
  loadAutoCoolSettings();
  lamp.begin();
  configureWebServer();
  printHelp();
  printState();
  Serial.flush();
  if (settings.ssid[0]) connectWifi();
  else beginWifiSetup();
}

void loop() {
  readSerial();
  lamp.service(setupStep == SetupStep::None);
  serviceTemperature();
  serviceAutoCool();
  if (sensorStatus != SensorStatus::Waiting) {
    const uint32_t now = esp_timer_get_time() / 1000000;
    // A stalled/slow web client must not turn an old reading into a fresh sample.
    const bool fresh = now - measuredTemperatureSeconds <=
        (kTemperatureInterval + kTemperatureConversionTime) / 1000 + 1;
    temperatureHistory.record(now, sensorStatus == SensorStatus::Ready && fresh,
                              measuredTemperature);
  }
  serviceNetwork();
  serviceCpuPower();
  delay(20);
}
