#!/usr/bin/env python3
"""Exercise the real BLE worker with simulated host callbacks and FreeRTOS queues.

No radio is used. The fake host deliberately keeps disconnect asynchronous and
rejects clearing objects before a successful stop, reproducing lifecycle faults.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

class String : public std::string {
 public:
  using std::string::operator=;
  String() = default;
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
  template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  explicit String(T n) : std::string(std::to_string(n)) {}
  void trim() {
    const auto first = find_first_not_of(" \t\r\n");
    *this = first == npos ? "" : substr(first, find_last_not_of(" \t\r\n") - first + 1);
  }
  void toLowerCase() { for (char &c : *this) c = std::tolower(static_cast<unsigned char>(c)); }
  bool startsWith(const char *s) const { return compare(0, std::strlen(s), s) == 0; }
  int indexOf(char c) const { const auto n = find(c); return n == npos ? -1 : int(n); }
  int indexOf(const char *s) const { const auto n = find(s); return n == npos ? -1 : int(n); }
  String substring(size_t begin) const { return substr(begin); }
  String substring(size_t begin, size_t end) const { return substr(begin, end - begin); }
  void toCharArray(char *out, size_t length) const { std::snprintf(out, length, "%s", c_str()); }
};
#define F(value) value
struct SerialStub {
  template<class T> void print(const T &) {}
  template<class T> void println(const T &) {}
  template<class... Args> void printf(const char *, Args...) {}
} Serial;
class Preferences {
 public:
  bool begin(const char *, bool) { return true; }
  size_t getBytesLength(const char *) { return 0; }
  size_t getBytes(const char *, void *, size_t) { return 0; }
  size_t putBytes(const char *, const void *, size_t size) { return size; }
  bool isKey(const char *) { return false; }
  bool remove(const char *) { return true; }
};
namespace fake {
uint32_t clock = 0;
int failStops = 0, createdClients = 0, successfulStops = 0, writes = 0;
int scanMode = 0; // 0: completes normally, 1: start failure, 2: no callback, 3: host reset.
bool initialized = false;
std::function<void()> onDelay;
std::function<void(unsigned)> beforeJob;
std::vector<std::vector<uint8_t>> results;
struct Finished {};
}
uint32_t millis() { return fake::clock; }
using TickType_t = uint32_t;
constexpr int pdTRUE = 1, pdPASS = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(value) (value)
void vTaskDelay(uint32_t value) { fake::clock += value; if (fake::onDelay) fake::onDelay(); }
struct FakeQueue { size_t size; std::deque<std::vector<uint8_t>> items; unsigned received = 0; };
using QueueHandle_t = FakeQueue *;
QueueHandle_t xQueueCreate(unsigned, size_t size) { auto *queue = new FakeQueue; queue->size = size; return queue; }
void vQueueDelete(QueueHandle_t queue) { delete queue; }
int xQueueSend(QueueHandle_t queue, const void *value, TickType_t) {
  const auto *bytes = static_cast<const uint8_t *>(value);
  queue->items.emplace_back(bytes, bytes + queue->size); return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void *out, TickType_t wait) {
  if (queue->items.empty()) { if (wait) throw fake::Finished{}; return 0; }
  if (wait && fake::beforeJob) fake::beforeJob(queue->received++);
  std::memcpy(out, queue->items.front().data(), queue->size);
  queue->items.pop_front(); return pdTRUE;
}
int xQueueOverwrite(QueueHandle_t queue, const void *value) {
  const auto *bytes = static_cast<const uint8_t *>(value);
  fake::results.emplace_back(bytes, bytes + queue->size); return pdTRUE;
}
int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *, unsigned, void *, unsigned) {
  return pdPASS;
}
class NimBLEUUID {
  std::string value_;
 public:
  explicit NimBLEUUID(uint16_t value) {
    char text[37]; std::snprintf(text, sizeof(text), "0000%04x-0000-1000-8000-00805f9b34fb", value);
    value_ = text;
  }
  NimBLEUUID to128() const { return *this; }
  std::string toString() const { return value_; }
  bool operator==(const NimBLEUUID &other) const { return value_ == other.value_; }
};
class NimBLEAddress {
 public:
  NimBLEAddress(const std::string &, uint8_t) {}
  std::string toString() const { return "11:22:33:44:55:66"; }
};
class NimBLERemoteService;
class NimBLERemoteCharacteristic {
 public:
  NimBLERemoteService *service = nullptr;
  bool canWriteNoResponse() const { return true; }
  bool canWrite() const { return true; }
  bool writeValue(uint8_t *, size_t, bool) { ++fake::writes; return true; }
  NimBLERemoteService *getRemoteService() const { return service; }
  NimBLEUUID getUUID() const { return NimBLEUUID(0xfff3); }
};
class NimBLERemoteService {
 public:
  NimBLERemoteCharacteristic characteristic;
  std::vector<NimBLERemoteCharacteristic *> characteristics;
  NimBLERemoteService() : characteristics{&characteristic} { characteristic.service = this; }
  NimBLEUUID getUUID() const { return NimBLEUUID(0xfff0); }
  NimBLERemoteCharacteristic *getCharacteristic(const char *) { return &characteristic; }
  const std::vector<NimBLERemoteCharacteristic *> &getCharacteristics() { return characteristics; }
};
class NimBLEClient {
 public:
  bool connected = false, disconnecting = false;
  NimBLERemoteService service;
  std::vector<NimBLERemoteService *> services{&service};
  void setConnectTimeout(uint32_t) {}
  bool isConnected() const { return connected && !disconnecting; }
  bool disconnect() { disconnecting = true; return true; }
  bool connect(const NimBLEAddress &) {
    if (connected || disconnecting) return false;
    connected = true; return true;
  }
  NimBLERemoteService *getService(const char *) { return &service; }
  const std::vector<NimBLERemoteService *> &getServices(bool) { return services; }
};
class NimBLEAdvertisedDevice {
 public:
  bool isConnectable() const { return true; }
  std::string getName() const { return "Lotus test"; }
  bool isAdvertisingService(NimBLEUUID) const { return true; }
  NimBLEAddress getAddress() const { return NimBLEAddress("", 0); }
  uint8_t getAddressType() const { return 0; }
  int8_t getRSSI() const { return -40; }
};
class NimBLEScanResults {
 public:
  int getCount() const { return 1; }
  const NimBLEAdvertisedDevice *getDevice(int) const { static NimBLEAdvertisedDevice device; return &device; }
};
class NimBLEScanCallbacks {
 public:
  virtual ~NimBLEScanCallbacks() = default;
  virtual void onScanEnd(const NimBLEScanResults &, int) {}
};
class NimBLEScan {
 public:
  bool scanning = false;
  uint32_t started = 0, duration = 0;
  NimBLEScanCallbacks *callbacks = nullptr;
  void clearResults() { assert(!scanning); }
  void setScanCallbacks(NimBLEScanCallbacks *value) { callbacks = value; }
  void setActiveScan(bool) {}
  void setMaxResults(unsigned) {}
  void setInterval(unsigned) {}
  void setWindow(unsigned) {}
  bool start(uint32_t value) {
    if (fake::scanMode == 1) return false;
    scanning = true; started = millis(); duration = value; return true;
  }
  bool stop() { scanning = false; return true; }
  NimBLEScanResults getResults() { assert(!scanning); return {}; }
  void tick() {
    if (scanning && fake::scanMode != 2 && millis() - started >= duration) {
      scanning = false;
      callbacks->onScanEnd({}, fake::scanMode == 3 ? 22 : 0);
    }
  }
};
class NimBLEDevice {
 public:
  static std::vector<NimBLEClient *> clients;
  static NimBLEScan *scan;
  static bool init(const char *) { fake::initialized = true; return true; }
  static bool isInitialized() { return fake::initialized; }
  static bool deinit(bool clear) {
    // An unsuccessful host stop must retain every object and pointer.
    if (fake::initialized && fake::failStops) { --fake::failStops; assert(!clear); return false; }
    if (fake::initialized) ++fake::successfulStops;
    fake::initialized = false;
    if (scan) scan->scanning = false;
    for (auto *client : clients) { client->connected = false; client->disconnecting = false; }
    if (clear) {
      for (auto *client : clients) delete client;
      clients.clear(); delete scan; scan = nullptr;
    }
    return true;
  }
  static NimBLEClient *createClient() {
    assert(fake::initialized); ++fake::createdClients;
    auto *client = new NimBLEClient; clients.push_back(client); return client;
  }
  static NimBLEScan *getScan() { if (!scan) scan = new NimBLEScan; return scan; }
};
std::vector<NimBLEClient *> NimBLEDevice::clients;
NimBLEScan *NimBLEDevice::scan = nullptr;
'''

CHECKS = r'''
#include "fake.h"
// Expose worker inputs without replacing or extracting any production code.
#define private public
#include "lotus_lamp.h"
#undef private
#include "lotus_lamp.cpp"
String jsonString(const String &value) { return "\"" + value + "\""; }
using Controller = LotusLampController;
using Operation = Controller::Operation;
using Job = Controller::Job;
using Result = Controller::Result;
Job job(Operation operation, const char *address = "11:22:33:44:55:66") {
  Job result{}; result.operation = operation; result.settings = Controller::defaults();
  std::snprintf(result.settings.address, sizeof(result.settings.address), "%s", address);
  return result;
}
std::vector<Result> run(std::initializer_list<Job> jobs) {
  Controller controller;
  controller.jobs_ = xQueueCreate(1, sizeof(Job));
  controller.results_ = xQueueCreate(1, sizeof(Result));
  fake::results.clear();
  for (const auto &item : jobs) xQueueSend(controller.jobs_, &item, 0);
  fake::onDelay = [] { if (NimBLEDevice::scan) NimBLEDevice::scan->tick(); };
  try { controller.worker(); } catch (fake::Finished &) {}
  std::vector<Result> results;
  for (const auto &bytes : fake::results) {
    Result result{}; std::memcpy(&result, bytes.data(), sizeof(result)); results.push_back(result);
  }
  vQueueDelete(controller.jobs_); vQueueDelete(controller.results_);
  fake::failStops = 0; NimBLEDevice::deinit(false); NimBLEDevice::deinit(true);
  fake::beforeJob = {}; return results;
}
int main() {
  // A second pair while connected must drain the first session before connecting.
  const auto paired = run({job(Operation::Pair), job(Operation::Pair, "aa:bb:cc:dd:ee:ff"),
                           job(Operation::Control, "aa:bb:cc:dd:ee:ff")});
  assert(paired.size() == 3 && paired[0].success && paired[1].success && paired[2].success);
  assert(fake::createdClients == 2 && fake::writes == 1);

  // A failed host stop keeps the client alive. A later retry must still work.
  const int created = fake::createdClients;
  fake::beforeJob = [](unsigned index) {
    if (index == 1) fake::failStops = 1;
    if (index == 2) assert(NimBLEDevice::clients.size() == 1);
  };
  const auto retried = run({job(Operation::Pair), job(Operation::Pair, "aa:bb:cc:dd:ee:ff"),
                            job(Operation::Pair, "aa:bb:cc:dd:ee:ff")});
  assert(retried[0].success && !retried[1].success && retried[2].success);
  assert(fake::createdClients == created + 2);

  fake::scanMode = 1;
  const auto failedScan = run({job(Operation::Scan)});
  assert(!failedScan[0].success && std::strstr(failedScan[0].message, "could not start"));
  fake::scanMode = 2;
  fake::clock = UINT32_MAX - 1000; // The deadline must survive millis() rollover.
  const uint32_t start = millis();
  const auto timedOut = run({job(Operation::Scan)});
  assert(!timedOut[0].success && std::strstr(timedOut[0].message, "timed out"));
  assert(uint32_t(millis() - start) == 9000);
  fake::scanMode = 3;
  const auto interrupted = run({job(Operation::Scan)});
  assert(!interrupted[0].success && std::strstr(interrupted[0].message, "interrupted"));
  fake::scanMode = 0;
  const auto scanned = run({job(Operation::Scan)});
  assert(scanned[0].success && scanned[0].count == 1);
  std::puts("Lamp worker checks passed: reconnect, shutdown retry, scan errors/deadline/rollover.");
}
'''

with tempfile.TemporaryDirectory(prefix="room-lamp-worker-") as directory:
    path = Path(directory)
    (path / "fake.h").write_text(STUBS)
    (path / "freertos").mkdir()
    for header in ["Arduino.h", "Preferences.h", "NimBLEDevice.h",
                   "freertos/FreeRTOS.h", "freertos/queue.h"]:
        (path / header).write_text('#include "fake.h"\n')
    (path / "test.cpp").write_text(CHECKS)
    binary = path / "test"
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(path), "-I", str(ROOT / "src"),
                    str(path / "test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
