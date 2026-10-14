"""Run firmware integration helpers with fake IR/NVS, using only host tools."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/main.cpp").read_text()
names = ["temperatureAvailable", "serviceActivityLed", "sendAcPacket", "restoreAutoCoolTarget",
         "stopAutoCool", "serviceAutoCool", "saveAcState",
         "saveAutoCoolSettings", "loadAutoCoolSettings"]
functions = []
for name in names:
    matches = re.findall(r"^(?:void|bool) " + name +
                         r"\([^;]*?\) \{[\s\S]*?^\}", source, re.MULTILINE)
    assert len(matches) == 1, f"Expected one definition of {name}"
    functions.append(matches[0])

fixture = r'''
#include "auto_cool.h"
#include "activity_led.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define F(value) value
const uint16_t kIrLed = 23;
const uint8_t kBluestarCool = 8, kBluestarMinTemp = 16, kBluestarMaxTemp = 30;
const uint32_t kAcStateVersion = 1;
const size_t kBluestarStateLength = 12;
uint32_t clockMs = 0, commandsSent = 0;
uint32_t millis() { return clockMs; }
uint64_t esp_timer_get_time() { return uint64_t(clockMs) * 1000; }
void boostCpu() {}
ActivityLed activityLed;
const uint8_t kActivityLedPin = 2, kActivityLedOnLevel = 1;
int ledLevel = 0;
void digitalWrite(uint8_t, uint8_t level) { ledLevel = level; }
struct SerialStub {
  void println(const char*) {}
  template<class... Args> void printf(const char*, Args...) {}
} Serial;
std::vector<std::array<uint8_t, 12>> packets;
struct IRBluestarAc {
  std::array<uint8_t, 12> raw{{1, 8, 24, 0}};
  explicit IRBluestarAc(uint16_t = 23) {}
  uint8_t* getRaw() { return raw.data(); }
  bool setRaw(const uint8_t* data) { std::memcpy(raw.data(), data, raw.size()); return true; }
  bool getPower() { return raw[0]; }
  uint8_t getMode() { return raw[1]; }
  uint8_t getTemp() { return raw[2]; }
  bool getTurbo() { return raw[3]; }
  void setPower(bool on) { raw[0] = on; if (raw[2] < 24) raw[2] = 24; raw[4] = 1; }
  void setTemp(uint8_t target) { raw[2] = target; raw[4] = 2; }
  void setTurbo(bool on) { raw[3] = on; raw[4] = 3; }
  void send() { assert(ledLevel == kActivityLedOnLevel); packets.push_back(raw); }
} ac;
struct LampStub {
  bool active = false;
  bool needsFastCpu() { return active; }
} lamp;
struct PreferencesStub {
  std::map<std::string, std::vector<uint8_t>> values;
  unsigned writes = 0;
  bool fail = false;
  size_t getBytesLength(const char* key) { return values[key].size(); }
  size_t getBytes(const char* key, void* out, size_t size) {
    if (values[key].size() != size) return 0;
    std::memcpy(out, values[key].data(), size); return size;
  }
  size_t putBytes(const char* key, const void* data, size_t size) {
    if (fail) return 0;
    const auto* first = static_cast<const uint8_t*>(data);
    values[key] = std::vector<uint8_t>(first, first + size); ++writes; return size;
  }
} storage;
struct SavedAcState { uint32_t version; uint8_t raw[12]; uint8_t normalFan; };
struct SavedAutoCool { uint32_t version; uint8_t target, enabled; uint16_t reserved; };
SavedAcState lastSavedAcState{};
SavedAutoCool lastSavedAutoCool{};
bool storageReady = true, haveSavedAcState = false, haveSavedAutoCool = false;
bool autoCoolSettingsSaved = true;
uint8_t normalFan = 4;
enum class SensorStatus { Waiting, Ready, Unavailable };
SensorStatus sensorStatus = SensorStatus::Ready;
enum class SetupStep { None, Ssid, Password };
SetupStep setupStep = SetupStep::None;
uint32_t measuredTemperatureSeconds = 0;
float measuredTemperature = 0;
AutoCool autoCool;
bool saveAcState();
bool saveAutoCoolSettings();
'''

checks = r'''
void read(float temperature, uint32_t elapsed = 5000) {
  clockMs += elapsed;
  measuredTemperature = temperature;
  measuredTemperatureSeconds = clockMs / 1000;
  serviceAutoCool();
}
int main() {
  ActivityLed led;
  assert(!led.on(0, false, false));
  assert(led.on(999, false, true) && !led.on(1000, false, true));
  assert(!led.on(1999, false, true) && led.on(2000, false, true));
  assert(led.on(1500, true, false) && led.on(1500, true, true));
  led.acCommand(1000);
  assert(led.on(1499, false, false) && !led.on(1500, false, false));
  assert(led.on(1499, false, true) && !led.on(1500, false, true));
  led.acCommand(UINT32_MAX - 200);
  assert(led.on(100, false, false) && !led.on(300, false, false));
  ac.setTemp(20); autoCool.enable(20);
  serviceActivityLed();
  assert(ledLevel == 0);  // Enabled but monitoring: no heartbeat.
  ac.setTurbo(true);
  serviceActivityLed(); assert(ledLevel == 1);
  clockMs = 1000;
  serviceActivityLed(); assert(ledLevel == 0);
  autoCool.disable(); clockMs = 2000;
  serviceActivityLed(); assert(ledLevel == 0);  // Manual Turbo is not Auto-Cool.
  lamp.active = true;
  serviceActivityLed(); assert(ledLevel == 1);
  lamp.active = false; ac.setTurbo(false); autoCool.enable(20); clockMs = 0;
  assert(saveAcState() && saveAutoCoolSettings());
  assert(storage.getBytesLength("auto-cool") == 8 && storage.writes == 2);
  assert(saveAcState() && saveAutoCoolSettings() && storage.writes == 2);
  read(22);
  assert(ac.getTurbo() && storage.writes == 2);  // Turbo must not write flash.
  for (int i = 1; i <= 60; ++i) read(22 + i / 100.0f);
  assert(autoCool.waiting() && !ac.getPower() && storage.writes == 3);
  clockMs += 1000;  // Let the Off-command activity pulse expire.
  serviceActivityLed();
  assert(ledLevel == 0);  // Recovery wait has Turbo set but AC is off.
  SavedAcState saved{};
  storage.getBytes("ac-state", &saved, sizeof(saved));
  assert(saved.raw[0] == 0 && saved.raw[2] == 20 && saved.raw[3] == 0);
  const AutoCool running = autoCool;
  autoCool = AutoCool();
  loadAutoCoolSettings();  // Reboot while saved power is Off cannot resume recovery.
  assert(!autoCool.enabled() && !autoCool.waiting());
  autoCool = running;
  for (int i = 0; i < 24; ++i) read(23);
  assert(ac.getPower() && ac.getTemp() == 20 && storage.writes == 4);
  assert(packets[packets.size() - 2][4] == 1);  // Power packet first, clamped to 24.
  assert(packets.back()[4] == 2 && packets.back()[2] == 20);  // Then restore target.
  autoCool = AutoCool();
  loadAutoCoolSettings();
  assert(autoCool.enabled() && autoCool.target() == 20);
  assert(!autoCool.waiting() && !autoCool.recoveryUsed() && !ac.getTurbo());
  const unsigned writes = storage.writes;
  read(22); read(19);
  assert(!ac.getTurbo() && storage.writes == writes);
  autoCool.setTarget(21); ac.setTemp(21);
  assert(saveAcState() && saveAutoCoolSettings());
  autoCool = AutoCool(); loadAutoCoolSettings();
  assert(autoCool.enabled() && autoCool.target() == 21);
  stopAutoCool();
  autoCool = AutoCool(); loadAutoCoolSettings();
  assert(!autoCool.enabled());  // Manual disable survives reboot.
  storage.fail = true;
  autoCool.enable(22);
  assert(!saveAutoCoolSettings() && !autoCoolSettingsSaved);
  storage.fail = false;
  autoCool = AutoCool(); loadAutoCoolSettings();
  assert(!autoCool.enabled());  // Failed write retains previous settings.
  storage.values["auto-cool"][0] = 99;
  autoCool = AutoCool(); loadAutoCoolSettings();
  assert(!autoCool.enabled());  // Invalid version fails closed.
  std::puts("Auto-Cool integration checks passed: IR target restore, flash writes, reboot, and storage failure.");
}
'''

with tempfile.TemporaryDirectory(prefix="room-auto-cool-") as directory:
    harness = pathlib.Path(directory) / "test.cpp"
    executable = pathlib.Path(directory) / "test"
    harness.write_text(fixture + "\n\n".join(functions) + checks)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "src"), str(harness), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
