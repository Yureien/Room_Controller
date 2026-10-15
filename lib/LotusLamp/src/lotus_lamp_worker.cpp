#include "lotus_lamp.h"
#include <NimBLEDevice.h>
#include <cstdio>
#include <cstring>

namespace {
std::string fullUuid(NimBLEUUID uuid) { return uuid.to128().toString(); }
bool writable(const NimBLERemoteCharacteristic *characteristic) {
  return characteristic && (characteristic->canWriteNoResponse() || characteristic->canWrite());
}

// The host finishes updating scan results before publishing completion. Merely
// polling isScanning() would let the worker read them while a callback runs.
class ScanCompletion : public NimBLEScanCallbacks {
 public:
  void reset() { reason_.store(kPending); }
  bool finished() const { return reason_.load() != kPending; }
  bool succeeded() const { return reason_.load() == 0; }
  void onScanEnd(const NimBLEScanResults &, int reason) override {
    reason_.store(reason);
  }

 private:
  static constexpr int kPending = -1;
  std::atomic<int> reason_{kPending};
};
}

struct LotusLampController::Worker {
  explicit Worker(LotusLampController& owner) : owner(owner) {}
  LotusLampController& owner;
  NimBLEClient *client = nullptr;
  NimBLERemoteCharacteristic *writer = nullptr;
  Settings active{};
  bool initialized = false;
  bool shutdownPending = false;
  bool shutdownFault = false;
  uint32_t lastActivity = 0;
  ScanCompletion scanCompletion;

  static void message(Result& result, const char* text) {
    snprintf(result.message, sizeof(result.message), "%s", text);
  }
  bool shutdown() {
    // NimBLE's disconnect() only requests termination. Stopping the host first
    // drains its callbacks, so neither a new connection nor object deletion can
    // race the previous disconnect. deinit(true) alone also clears objects when
    // stopping fails; retain them until a successful stop instead.
    if (shutdownFault) return false;
    owner.connected_.store(false);
    if (initialized && !NimBLEDevice::deinit(false)) {
      // IDF controller teardown may fail after NimBLE has already stopped.
      // A subsequent deinit would report success without retrying that layer.
      shutdownFault = !NimBLEDevice::isInitialized();
      shutdownPending = true;
      return false;
    }
    if (!NimBLEDevice::deinit(true)) {
      shutdownPending = true;
      return false;
    }
    writer = nullptr;
    client = nullptr;
    initialized = false;
    shutdownPending = false;
    owner.connected_.store(false);
    owner.radioActive_.store(false);
    return true;
  }
  bool initialize() {
    if (shutdownPending && !shutdown()) return false;
    if (!initialized) initialized = NimBLEDevice::init("");
    owner.radioActive_.store(initialized);
    return initialized;
  }
  void scanDevices(Result& result) {
    if (client && (!shutdown() || !initialize())) {
      message(result, "Could not restart Bluetooth for scanning; retry Disconnect.");
      return;
    }
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->clearResults();
    scanCompletion.reset();
    scan->setScanCallbacks(&scanCompletion);
    scan->setActiveScan(true);
    scan->setMaxResults(32);
    scan->setInterval(100);
    scan->setWindow(50);
    if (!scan->start(8000)) {
      message(result, "Bluetooth scan could not start; retry Scan.");
      return;
    }
    const uint32_t started = millis();
    while (!scanCompletion.finished() && millis() - started < 9000) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!scanCompletion.finished()) {
      // Do not inspect or clear results after a timeout: a callback might
      // still own them. The checked shutdown below will drain the host.
      message(result, scan->stop() ? "Bluetooth scan timed out; retry Scan." :
          "Bluetooth scan could not stop; retry Disconnect.");
      return;
    }
    if (!scanCompletion.succeeded()) {
      message(result, "Bluetooth scan interrupted; retry Scan.");
      return;
    }
    const NimBLEScanResults found = scan->getResults();
    // Show probable lamps first, but retain other connectable devices for variants.
    for (int pass = 0; pass < 2; pass++) {
      for (int i = 0; i < found.getCount() && result.count < kMaxDevices; i++) {
        const NimBLEAdvertisedDevice *device = found.getDevice(i);
        if (!device->isConnectable()) continue;
        String name(device->getName().c_str());
        String lower = name;
          lower.toLowerCase();
        const bool likely = device->isAdvertisingService(NimBLEUUID(uint16_t(0xfff0))) ||
            lower.indexOf("melk") >= 0 || lower.indexOf("lotus") >= 0 ||
            lower.indexOf("led") >= 0 || lower.indexOf("rgb") >= 0 || lower.indexOf("lamp") >= 0;
        if (likely != (pass == 0)) continue;
        Device &out = result.devices[result.count++];
        snprintf(out.address, sizeof(out.address), "%s", device->getAddress().toString().c_str());
        snprintf(out.name, sizeof(out.name), "%s", name.length() ? name.c_str() : "Unnamed device");
        out.type = device->getAddressType();
          out.rssi = device->getRSSI();
          out.likely = likely;
      }
    }
    scan->clearResults();
    result.success = true;
    snprintf(result.message, sizeof(result.message), "Scan found %u connectable devices. Use lamp pair <number>.", result.count);
  }
  bool connect(const Job& job, Result& result) {
    const bool reuse = client && client->isConnected() && writer &&
        std::strcmp(active.address, job.settings.address) == 0 &&
        active.addressType == job.settings.addressType &&
        std::strcmp(active.service, job.settings.service) == 0 &&
        std::strcmp(active.characteristic, job.settings.characteristic) == 0;
    if (!reuse) {
      // A different device/UUID or a lost connection gets a fresh session.
      // Never connect while the old client's disconnect is still pending.
      const bool ready = (!client || shutdown()) && initialize();
      owner.connected_.store(false);
      if (!ready) {
        message(result, "Could not restart Bluetooth; retry Disconnect.");
        return false;
      } else {
        client = NimBLEDevice::createClient();
        if (client) client->setConnectTimeout(8000);
      }
      if (ready && (!client || !client->connect(NimBLEAddress(std::string(job.settings.address), job.settings.addressType)))) {
        message(result, "Could not connect. Close the phone's lamp app, check power/range, then retry.");
        return false;
      } else if (ready) {
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
          message(result, "No unambiguous writable lamp characteristic. Use lamp pair N SERVICE_UUID WRITE_UUID; see UUIDs below.");
          client->disconnect();
        } else {
          snprintf(result.settings.service, sizeof(result.settings.service), "%s", fullUuid(writer->getRemoteService()->getUUID()).c_str());
          snprintf(result.settings.characteristic, sizeof(result.settings.characteristic), "%s", fullUuid(writer->getUUID()).c_str());
          active = result.settings;
        }
      }
    }
    return writer && client && client->isConnected();
  }
  void write(const Job& job, Result& result) {
    if (!shutdownPending && !shutdownFault && writer && client && client->isConnected()) {
      if (job.operation != Operation::Control) {
        result.success = true;
        message(result, "Lamp connected. Device selection saved; no light settings replayed.");
      } else {
        uint8_t packet[9];
        lotus::encode(job.control, packet);
        if (writer->writeValue(packet, sizeof(packet), !writer->canWriteNoResponse())) {
          result.success = true;
          message(result, "Lamp command written over Bluetooth.");
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
          message(result, "Bluetooth write failed. Check the strip before retrying.");
          client->disconnect();
          writer = nullptr;
        }
      }
    }
  }
  void run() {
    for (;;) {
      Job job{};
      // Block indefinitely with the radio off. Wake only for a user request.
      if (xQueueReceive(owner.jobs_, &job, initialized ? pdMS_TO_TICKS(1000) : portMAX_DELAY) != pdTRUE) {
        owner.connected_.store(!shutdownPending && !shutdownFault && client && client->isConnected() && writer);
        if (!owner.connected_.load() || millis() - lastActivity >= 30000) shutdown();
        continue;
      }
      Result result{};
      result.operation = job.operation;
      result.settings = job.settings;
      if (job.operation == Operation::Forget || job.operation == Operation::Disconnect) {
        result.success = shutdown();
        message(result, !result.success ? "Bluetooth shutdown failed; retry Disconnect." :
            job.operation == Operation::Forget ? "Lamp device forgotten." :
            "Bluetooth disconnected; strip settings retained.");
      } else {
        if (!initialize()) message(result, "Bluetooth initialization failed.");
        else if (job.operation == Operation::Scan) {
          scanDevices(result);
        } else {
          if (connect(job, result)) write(job, result);

        }
      }
      owner.connected_.store(!shutdownPending && !shutdownFault && client && client->isConnected() && writer);
      lastActivity = millis();
      // Scans and failed connections must not leave an idle radio running.
      if (!owner.connected_.load() && !shutdown()) {
        result.success = false;
        message(result, "Bluetooth shutdown failed; retry Disconnect.");
      }
      if (shutdownFault)
        message(result, "Bluetooth controller teardown failed; restart the board before using BLE.");
      xQueueOverwrite(owner.results_, &result);
    }
  }
};

void LotusLampController::workerEntry(void* self) {
  Worker worker(*static_cast<LotusLampController*>(self));
  worker.run();
}
