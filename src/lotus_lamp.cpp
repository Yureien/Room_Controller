#include "lotus_lamp.h"
#include <NimBLEDevice.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

// Shared JSON escaping; settings and NVS writes stay on the main loop.
String jsonString(const String &value);

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
std::string fullUuid(NimBLEUUID uuid) { return uuid.to128().toString(); }
bool writable(const NimBLERemoteCharacteristic *characteristic) {
  return characteristic && (characteristic->canWriteNoResponse() || characteristic->canWrite());
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
      Job job{};
      job.operation = Operation::Connect;
      job.settings = settings_;
      String ignored;
      // Connect only; do not replay light settings at boot.
      if (!enqueue(job, ignored)) { message_ = ignored; lastOk_ = false; }
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
  } else if (line == "reconnect") job.operation = Operation::Connect;
  else {
    job.operation = Operation::Control;
    if (!lotus::parse(line.c_str(), job.control)) {
      message = "Use lamp on/off, color <name|#RRGGBB>, brightness 0..100, effect 0..212, or speed 0..100.";
      return false;
    }
  }
  if (job.operation != Operation::Scan && job.operation != Operation::Pair &&
      job.operation != Operation::Forget && !settings_.address[0]) {
    message = "No lamp selected. Run lamp scan and lamp pair <number> over serial.";
    return false;
  }
  return enqueue(job, message);
}

void LotusLampController::workerEntry(void *self) {
  static_cast<LotusLampController *>(self)->worker();
}

void LotusLampController::worker() {
  NimBLEClient *client = nullptr;
  NimBLERemoteCharacteristic *writer = nullptr;
  Settings active{};
  bool initialized = false;
  for (;;) {
    Job job{};
    if (xQueueReceive(jobs_, &job, pdMS_TO_TICKS(250)) != pdTRUE) {
      connected_.store(client && client->isConnected() && writer);
      continue;
    }
    Result result{};
    result.operation = job.operation;
    result.settings = job.settings;
    auto fail = [&](const char *message) { snprintf(result.message, sizeof(result.message), "%s", message); };
    if (job.operation == Operation::Forget) {
      if (client && client->isConnected()) client->disconnect();
      writer = nullptr;
      result.success = true;
      fail("Lamp device forgotten.");
    } else {
      if (!initialized) initialized = NimBLEDevice::init("");
      if (!initialized) fail("Bluetooth initialization failed.");
      else if (job.operation == Operation::Scan) {
        if (client && client->isConnected()) client->disconnect();
        writer = nullptr;
        connected_.store(false);
        NimBLEScan *scan = NimBLEDevice::getScan();
        scan->clearResults();
        scan->setActiveScan(true);
        scan->setMaxResults(32);
        scan->setInterval(100);
        scan->setWindow(50);
        const NimBLEScanResults found = scan->getResults(8000);
        // Show probable lamps first, but retain other connectable devices for variants.
        for (int pass = 0; pass < 2; pass++) {
          for (int i = 0; i < found.getCount() && result.count < kMaxDevices; i++) {
            const NimBLEAdvertisedDevice *device = found.getDevice(i);
            if (!device->isConnectable()) continue;
            String name(device->getName().c_str());
            String lower = name; lower.toLowerCase();
            const bool likely = device->isAdvertisingService(NimBLEUUID(uint16_t(0xfff0))) ||
                lower.indexOf("melk") >= 0 || lower.indexOf("lotus") >= 0 ||
                lower.indexOf("led") >= 0 || lower.indexOf("rgb") >= 0 || lower.indexOf("lamp") >= 0;
            if (likely != (pass == 0)) continue;
            Device &out = result.devices[result.count++];
            snprintf(out.address, sizeof(out.address), "%s", device->getAddress().toString().c_str());
            snprintf(out.name, sizeof(out.name), "%s", name.length() ? name.c_str() : "Unnamed device");
            out.type = device->getAddressType(); out.rssi = device->getRSSI(); out.likely = likely;
          }
        }
        scan->clearResults();
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Scan found %u connectable devices. Use lamp pair <number>.", result.count);
      } else {
        if (!client) {
          client = NimBLEDevice::createClient();
          if (client) client->setConnectTimeout(8000);
        }
        const bool reuse = client && client->isConnected() && writer &&
            std::strcmp(active.address, job.settings.address) == 0 &&
            active.addressType == job.settings.addressType &&
            std::strcmp(active.service, job.settings.service) == 0 &&
            std::strcmp(active.characteristic, job.settings.characteristic) == 0;
        if (!reuse) {
          if (client && client->isConnected()) client->disconnect();
          connected_.store(false);
          writer = nullptr;
          if (!client || !client->connect(NimBLEAddress(std::string(job.settings.address), job.settings.addressType))) {
            fail("Could not connect. Close the phone's lamp app, check power/range, then retry.");
          } else {
            NimBLERemoteService *service = client->getService(job.settings.service);
            if (service) writer = service->getCharacteristic(job.settings.characteristic);
            if (!writable(writer)) writer = nullptr;
            if (!writer && job.operation == Operation::Pair && !job.explicitUuid) {
              NimBLERemoteCharacteristic *candidate = nullptr, *standard = nullptr;
              unsigned candidates = 0, standardCount = 0;
              size_t detailLength = 0;
              for (auto *remoteService : client->getServices(true)) {
                if (remoteService->getUUID() == NimBLEUUID(uint16_t(0x1800)) ||
                    remoteService->getUUID() == NimBLEUUID(uint16_t(0x1801))) continue;
                for (auto *characteristic : remoteService->getCharacteristics()) {
                  if (!writable(characteristic)) continue;
                  candidate = characteristic; candidates++;
                  if (characteristic->getUUID() == NimBLEUUID(uint16_t(0xfff3))) {
                    standard = characteristic; standardCount++;
                  }
                  if (detailLength + 80 < sizeof(result.details)) {
                    detailLength += snprintf(result.details + detailLength, sizeof(result.details) - detailLength,
                        "  %s %s\n", fullUuid(remoteService->getUUID()).c_str(), fullUuid(characteristic->getUUID()).c_str());
                  }
                }
              }
              if (standardCount == 1) writer = standard;
              else if (candidates == 1) writer = candidate;
            }
            if (!writer) {
              fail("No unambiguous writable lamp characteristic. Use lamp pair N SERVICE_UUID WRITE_UUID; see UUIDs below.");
              client->disconnect();
            } else {
              snprintf(result.settings.service, sizeof(result.settings.service), "%s", fullUuid(writer->getRemoteService()->getUUID()).c_str());
              snprintf(result.settings.characteristic, sizeof(result.settings.characteristic), "%s", fullUuid(writer->getUUID()).c_str());
              active = result.settings;
            }
          }
        }
        if (writer && client && client->isConnected()) {
          if (job.operation != Operation::Control) {
            result.success = true;
            fail("Lamp connected. Device selection saved; no light settings replayed.");
          } else {
            uint8_t packet[9]; lotus::encode(job.control, packet);
            if (writer->writeValue(packet, sizeof(packet), !writer->canWriteNoResponse())) {
              result.success = true;
              fail("Lamp command written over Bluetooth.");
              switch (job.control.action) {
                case lotus::Action::Power:
                  result.settings.power = job.control.values[0]; result.settings.powerKnown = true; break;
                case lotus::Action::Color:
                  std::memcpy(result.settings.color, job.control.values, 3); result.settings.solid = true; break;
                case lotus::Action::Brightness: result.settings.brightness = job.control.values[0]; break;
                case lotus::Action::Effect: result.settings.effect = job.control.values[0]; result.settings.solid = false; break;
                case lotus::Action::Speed: result.settings.speed = job.control.values[0]; break;
              }
              vTaskDelay(pdMS_TO_TICKS(job.control.action == lotus::Action::Power ? 500 : 100));
            } else {
              fail("Bluetooth write failed. Check the strip before retrying.");
              client->disconnect(); writer = nullptr;
            }
          }
        }
      }
    }
    connected_.store(client && client->isConnected() && writer);
    xQueueOverwrite(results_, &result);
  }
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
    } else {
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
