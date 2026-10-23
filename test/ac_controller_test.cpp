#include "ac_controller.h"
#include <cassert>
#include <cstdio>

SerialFake Serial;
StorageFake fakeStorage;
uint32_t clockMs = 0;
std::vector<Packet> packets;
unsigned beforeCount = 0, afterCount = 0;
void beforeIr() { assert(beforeCount == afterCount); ++beforeCount; }
void afterIr() { assert(beforeCount == afterCount + 1); ++afterCount; }

struct Rig {
  AcController ac{23};
  TemperatureReading reading;
  Rig() {
    fakeStorage = StorageFake();
    packets.clear();
    clockMs = 0;
    reading.status = TemperatureReading::Status::Ready;
    reading.celsius = 27;
    ac.begin(beforeIr, afterIr);
    assert(packets.empty());
    command("on");
    command("temp 20");
  }
  void command(const char* text, bool success = true) {
    String message;
    assert(ac.command(text, &message, reading) == success);
  }
  void read(float temperature, uint32_t elapsed = 5000) {
    clockMs += elapsed;
    reading.celsius = temperature;
    reading.sampledAtSeconds += elapsed / 1000;
    ac.service(reading, false);
  }
  String state() { String result; ac.appendJson(result); return result; }
  void has(const char* field) { assert(state().find(field) != String::npos); }
};

int main() {
  // Manual Turbo expires without a browser, new sample, or valid sensor.
  Rig manual;
  manual.command("turbo on");
  const auto writes = fakeStorage.writes;
  manual.read(27, TurboTimer::kDurationMs - 1);
  manual.has("\"turbo\":true");
  manual.command("horizontal on");  // Unrelated packets cannot extend Turbo.
  manual.reading.status = TemperatureReading::Status::Unavailable;
  manual.read(27, 1);
  manual.has("\"turbo\":false");
  manual.has("\"temperature\":20");
  manual.has("\"fan\":4");
  manual.has("\"turboCooldownSeconds\":300");
  assert(fakeStorage.writes == writes + 2);  // Swing edit and manual expiry.
  const auto count = packets.size();
  manual.read(27);
  assert(packets.size() == count);  // Expiry emits exactly one Off packet.

  // Auto-Cool stays in normal cooling for five minutes, then may retry.
  Rig automatic;
  automatic.command("autocool on");
  const auto autoWrites = fakeStorage.writes;
  automatic.read(22);
  automatic.has("\"turbo\":true");
  for (int i = 0; i < 240; ++i) automatic.read(22);
  automatic.has("\"turbo\":false");
  automatic.has("\"turboCooldownSeconds\":300");
  for (int i = 0; i < 59; ++i) automatic.read(22);
  automatic.has("\"turbo\":false");
  automatic.read(22);
  automatic.has("\"turbo\":true");
  assert(fakeStorage.writes == autoWrites);

  // Explicit manual activation can start a fresh session during cooldown.
  automatic.command("turbo on");
  automatic.has("\"enabled\":false");
  automatic.read(22, TurboTimer::kDurationMs);
  automatic.has("\"turbo\":false");

  // Manual Off cancels the timer and cannot later send an On packet.
  Rig off;
  off.command("turbo on");
  off.command("off");
  const auto offPackets = packets.size();
  off.read(27, TurboTimer::kDurationMs);
  off.has("\"power\":false");
  assert(packets.size() == offPackets);

  // Raw imports are inert until sent, even with Turbo flagged On.
  Rig imported;
  imported.command("turbo on");
  const auto raw = packets.back();
  imported.command("turbo off");
  char loadCommand[30] = "load ";
  for (size_t i = 0; i < raw.size(); ++i)
    std::sprintf(loadCommand + 5 + i * 2, "%02x", raw[i]);
  imported.command(loadCommand);
  const auto loadedPackets = packets.size();
  imported.read(27, TurboTimer::kDurationMs);
  assert(packets.size() == loadedPackets);
  imported.command("send");
  imported.read(27, TurboTimer::kDurationMs);
  imported.has("\"turbo\":false");

  // Reboot cannot grant a persisted Turbo session another twenty minutes.
  Rig reboot;
  reboot.command("turbo on");
  const auto bootPackets = packets.size();
  AcController restored(23);
  restored.begin(beforeIr, afterIr);
  String state;
  restored.appendJson(state);
  assert(state.find("\"turbo\":false") != String::npos);
  assert(packets.size() == bootPackets);

  // Both deadlines survive millis() wrapping; early Off stops tracking.
  TurboTimer timer;
  const uint32_t start = UINT32_MAX - 1000;
  timer.sent(start, true, true);
  assert(!timer.expire(start + TurboTimer::kDurationMs - 1));
  assert(timer.expire(start + TurboTimer::kDurationMs));
  assert(timer.cooldownSeconds(start + TurboTimer::kDurationMs) == 300);
  assert(timer.cooldownSeconds(start + TurboTimer::kDurationMs + TurboTimer::kCooldownMs) == 0);
  timer.sent(0, true, true);
  timer.sent(1, true, false);
  assert(!timer.expire(TurboTimer::kDurationMs));
  assert(beforeCount == afterCount);
  std::puts("AcController host checks passed: Turbo expiry, cooldown, IR hooks, NVS, raw import, reboot, rollover.");
}
