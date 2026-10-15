#include "lotus_lamp.h"
#include "json_utils.h"
#include <algorithm>
#include <cstdio>
#include <cstring>


namespace {
const char *kService = "0000fff0-0000-1000-8000-00805f9b34fb";
const char *kCharacteristic = "0000fff3-0000-1000-8000-00805f9b34fb";
bool validUuid(const char *text) {
  if (std::strlen(text) != 36) return false;
  for (unsigned i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (text[i] != '-') return false;
    } else if (lotus::hexDigit(text[i]) < 0) return false;
  }
  return true;
}
bool validAddress(const char *text) {
  if (std::strlen(text) != 17) return false;
  for (unsigned i = 0; i < 17; i++) {
    if (i % 3 == 2) { if (text[i] != ':') return false; }
    else if (lotus::hexDigit(text[i]) < 0) return false;
  }
  return std::strcmp(text, "00:00:00:00:00:00") != 0;
}
}

LotusLampController::Settings LotusLampController::defaults() {
  Settings value{};
  value.version = kVersion;
  snprintf(value.service, sizeof(value.service), "%s", kService);
  snprintf(value.characteristic, sizeof(value.characteristic), "%s", kCharacteristic);
  value.color[0] = value.color[1] = value.color[2] = 255;
  value.brightness = 100;
  value.speed = 50;
  value.solid = true;
  return value;
}

bool LotusLampController::validSettings(const Settings &value) {
  return value.version == kVersion &&
      std::memchr(value.address, 0, sizeof(value.address)) &&
      std::memchr(value.name, 0, sizeof(value.name)) &&
      std::memchr(value.service, 0, sizeof(value.service)) &&
      std::memchr(value.characteristic, 0, sizeof(value.characteristic)) &&
      validAddress(value.address) && validUuid(value.service) &&
      validUuid(value.characteristic) && value.addressType <= 3 &&
      value.brightness <= 100 && value.speed <= 100 && value.effect <= 212 &&
      value.power <= 1 && value.powerKnown <= 1 && value.solid <= 1;
}

void LotusLampController::begin() {
  settings_ = defaults();
  message_ = "Set up your strip over serial: lamp scan, then lamp pair <number>.";
  storageReady_ = storage_.begin("lotus-lamp", false);
  if (!storageReady_) message_ = "Lamp flash storage unavailable; device cannot be saved.";
  if (storageReady_ && storage_.getBytesLength("settings") != 0) {
    Settings value{};
    if (storage_.getBytesLength("settings") == sizeof(value) &&
        storage_.getBytes("settings", &value, sizeof(value)) == sizeof(value) &&
        validSettings(value)) {
      settings_ = saved_ = value;
      haveSaved_ = true;
      // Leave Bluetooth off until a command or explicit connection request.
      message_ = "Strip saved. Connect or send a setting when needed; Bluetooth sleeps after 30s idle.";
    } else message_ = "Saved lamp device invalid; run lamp scan to set it up again.";
  }
}

bool LotusLampController::save() {
  if (haveSaved_ && std::memcmp(&settings_, &saved_, sizeof(settings_)) == 0) return true;
  if (!storageReady_ ||
      storage_.putBytes("settings", &settings_, sizeof(settings_)) != sizeof(settings_)) return false;
  saved_ = settings_;
  haveSaved_ = true;
  return true;
}

bool LotusLampController::enqueue(const Job &job, String &message) {
  if (busy_) { message = "Lamp is busy; wait for the current operation."; return false; }
  if (!jobs_) {
    jobs_ = xQueueCreate(1, sizeof(Job));
    results_ = xQueueCreate(1, sizeof(Result));
    if (!jobs_ || !results_ ||
        xTaskCreatePinnedToCore(workerEntry, "lotus-ble", 12288, this, 1, nullptr, 0) != pdPASS) {
      if (jobs_) vQueueDelete(jobs_);
      if (results_) vQueueDelete(results_);
      jobs_ = results_ = nullptr;
      message = "Not enough memory to start Bluetooth.";
      return false;
    }
  }
  if (xQueueSend(jobs_, &job, 0) != pdTRUE) {
    message = "Lamp queue unavailable.";
    return false;
  }
  busy_ = true;
  lastOk_ = true;
  message_ = job.operation == Operation::Scan ? "Scanning Bluetooth for 8 seconds..." :
      "Working on lamp command...";
  message = message_;
  return true;
}

bool LotusLampController::command(String line, bool allowSetup, String &message) {
  line.trim();
  line.toLowerCase();
  if (line == "status" || line.length() == 0) { message = message_; return true; }
  Job job{};
  job.settings = settings_;
  if (line == "scan" || line == "forget" || line.startsWith("pair ")) {
    if (!allowSetup) { message = "Lamp setup is available over serial."; return false; }
    if (line == "scan") job.operation = Operation::Scan;
    else if (line == "forget") job.operation = Operation::Forget;
    else {
      String values = line.substring(5);
      const int space = values.indexOf(' ');
      String index = space < 0 ? values : values.substring(0, space);
      uint8_t number;
      if (!lotus::number(index.c_str(), deviceCount_, number) || number == 0) {
        message = "Run lamp scan, then lamp pair <number from the list>.";
        return false;
      }
      const Device &device = devices_[number - 1];
      job.operation = Operation::Pair;
      job.settings = defaults();
      snprintf(job.settings.address, sizeof(job.settings.address), "%s", device.address);
      snprintf(job.settings.name, sizeof(job.settings.name), "%s", device.name);
      job.settings.addressType = device.type;
      if (space >= 0) {
        values = values.substring(space + 1);
        const int split = values.indexOf(' ');
        if (split < 0) { message = "Supply both service and write UUIDs."; return false; }
        const String service = values.substring(0, split);
        const String characteristic = values.substring(split + 1);
        if (!validUuid(service.c_str()) || !validUuid(characteristic.c_str())) {
          message = "Use full 36-character service and write UUIDs."; return false;
        }
        service.toCharArray(job.settings.service, sizeof(job.settings.service));
        characteristic.toCharArray(job.settings.characteristic, sizeof(job.settings.characteristic));
        job.explicitUuid = true;
      }
    }
  } else if (line == "reconnect" || line == "connect") job.operation = Operation::Connect;
  else if (line == "disconnect") job.operation = Operation::Disconnect;
  else {
    job.operation = Operation::Control;
    if (!lotus::parse(line.c_str(), job.control)) {
      message = "Use lamp on/off, color <name|#RRGGBB>, brightness 0..100, effect 0..212, or speed 0..100.";
      return false;
    }
  }
  if (job.operation != Operation::Scan && job.operation != Operation::Pair &&
      job.operation != Operation::Forget && job.operation != Operation::Disconnect &&
      !settings_.address[0]) {
    message = "No lamp selected. Run lamp scan and lamp pair <number> over serial.";
    return false;
  }
  return enqueue(job, message);
}

void LotusLampController::service(bool printEvents) {
  if (!results_) return;
  Result result{};
  if (xQueueReceive(results_, &result, 0) != pdTRUE) return;
  busy_ = false;
  completed_++;
  lastOk_ = result.success;
  message_ = result.message;
  if (result.success) {
    if (result.operation == Operation::Scan) {
      deviceCount_ = result.count;
      std::memcpy(devices_, result.devices, sizeof(devices_));
    } else if (result.operation == Operation::Forget) {
      if (!storageReady_ || (storage_.isKey("settings") && !storage_.remove("settings"))) {
        lastOk_ = false; message_ = "Could not forget saved lamp; previous selection retained.";
      } else { settings_ = defaults(); haveSaved_ = false; }
    } else if (result.operation != Operation::Disconnect) {
      settings_ = result.settings;
      if (!save()) { lastOk_ = false; message_ += " Warning: could not save lamp settings to flash."; }
    }
  }
  if (!printEvents) return;
  Serial.print(F("Lamp: ")); Serial.println(message_);
  if (result.details[0]) { Serial.println(F("Writable service / characteristic UUIDs:")); Serial.print(result.details); }
  if (result.success && result.operation == Operation::Scan) {
    for (uint8_t i = 0; i < deviceCount_; i++) {
      const Device &device = devices_[i];
      Serial.printf("  %u: %s | %s | %d dBm | %s%s\n", i + 1, device.name, device.address,
          device.rssi, device.type == 0 ? "public" : "random", device.likely ? " | likely lamp" : "");
    }
  }
}

String LotusLampController::json() const {
  String result; result.reserve(512);
  result = "{\"configured\":" + String(settings_.address[0] ? "true" : "false");
  result += ",\"connected\":" + String(connected_.load() ? "true" : "false");
  result += ",\"busy\":" + String(busy_ ? "true" : "false");
  result += ",\"ok\":" + String(lastOk_ ? "true" : "false");
  result += ",\"message\":" + jsonString(message_);
  result += ",\"name\":" + jsonString(String(settings_.name));
  result += ",\"address\":" + jsonString(String(settings_.address));
  result += ",\"power\":" + String(settings_.powerKnown ? (settings_.power ? "true" : "false") : "null");
  char color[8]; snprintf(color, sizeof(color), "#%02x%02x%02x", settings_.color[0], settings_.color[1], settings_.color[2]);
  result += ",\"color\":" + jsonString(String(color));
  result += ",\"brightness\":" + String(settings_.brightness);
  result += ",\"speed\":" + String(settings_.speed);
  result += ",\"effect\":" + String(settings_.effect);
  result += ",\"solid\":" + String(settings_.solid ? "true" : "false");
  result += ",\"completed\":" + String(completed_) + "}";
  return result;
}

void LotusLampController::printStatus() const {
  Serial.println(json());
  if (settings_.address[0]) {
    Serial.print(F("Lamp service: ")); Serial.println(settings_.service);
    Serial.print(F("Lamp write characteristic: ")); Serial.println(settings_.characteristic);
  }
}
