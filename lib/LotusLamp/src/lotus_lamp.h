#ifndef LOTUS_LAMP_H_
#define LOTUS_LAMP_H_

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <cstddef>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "lotus_protocol.h"

class LotusLampController {
 public:
  LotusLampController() = default;
  LotusLampController(const LotusLampController&) = delete;
  LotusLampController& operator=(const LotusLampController&) = delete;
  void begin();
  void service(bool printEvents = true);
  bool command(String line, bool allowSetup, String &message);
  String json() const;
  void printStatus() const;
  bool needsFastCpu() const { return busy_ || radioActive_.load(); }

 private:
  static constexpr uint32_t kVersion = 1;
  static constexpr uint8_t kMaxDevices = 16;
  struct Settings {
    uint32_t version;
    char address[18], name[48], service[37], characteristic[37];
    uint8_t addressType, color[3], brightness, speed, effect;
    uint8_t power, powerKnown, solid;
  };
  static_assert(sizeof(Settings) == 156, "Legacy lamp NVS layout changed");
  static_assert(offsetof(Settings, address) == 4 &&
                offsetof(Settings, service) == 70 &&
                offsetof(Settings, addressType) == 144 &&
                offsetof(Settings, power) == 151, "Legacy lamp NVS offsets changed");
  struct Device {
    char address[18], name[48];
    uint8_t type;
    int8_t rssi;
    bool likely;
  };
  enum class Operation : uint8_t { Scan, Pair, Connect, Disconnect, Forget, Control };
  struct Job {
    Operation operation;
    Settings settings;
    lotus::Command control;
    bool explicitUuid;
  };
  struct Result {
    Operation operation;
    bool success;
    Settings settings;
    uint8_t count;
    Device devices[kMaxDevices];
    char message[160];
    char details[768];  // Writable UUIDs for ambiguous/variant devices.
  };
  Preferences storage_;
  Settings settings_{}, saved_{};
  Device devices_[kMaxDevices]{};
  uint8_t deviceCount_ = 0;
  bool storageReady_ = false, haveSaved_ = false, busy_ = false;
  bool lastOk_ = true;
  String message_;
  uint32_t completed_ = 0;
  QueueHandle_t jobs_ = nullptr, results_ = nullptr;
  std::atomic<bool> connected_{false};
  std::atomic<bool> radioActive_{false};

  static Settings defaults();
  static bool validSettings(const Settings &value);
  bool save();
  bool enqueue(const Job &job, String &message);
  static void workerEntry(void *self);
  struct Worker;
};
#endif
