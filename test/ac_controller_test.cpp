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
  void failCooling() {
    read(27);
    for (int i = 0; i < 240; ++i) read(27);
    has("\"waiting\":true");
    has("\"power\":false");
  }
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
  for (int i = 0; i < 240; ++i) automatic.read(22.8f - i / 300.0f);
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

  // Recovery persists Off/On only, restores a sub-24 target in a separate
  // packet, rejects early On, and can retry with a larger cooldown.
  Rig recovery;
  recovery.command("autocool on");
  const auto recoveryWrites = fakeStorage.writes;
  recovery.failCooling();
  recovery.has("\"recoveryAttempts\":1");
  recovery.has("\"waitSeconds\":300");
  assert(fakeStorage.writes == recoveryWrites + 1);
  recovery.command("on", false);
  recovery.command("temp 21", false);
  recovery.command("state");
  const auto beforeRestart = packets.size();
  recovery.read(27, 300000);
  assert(packets.size() == beforeRestart + 2);
  const auto& powerOn = packets[packets.size() - 2];
  const auto& target = packets.back();
  assert((powerOn[2] & 0x80) && (powerOn[3] & 31) == 24);
  assert(((powerOn[10] << 8) | powerOn[11]) == kBluestarCommandPower);
  assert((target[3] & 31) == 20);
  assert(((target[10] << 8) | target[11]) == kBluestarCommandTemp);
  recovery.has("\"recoveryBackoffSeconds\":600");
  assert(fakeStorage.writes == recoveryWrites + 2);
  for (int i = 0; i < 240; ++i) recovery.read(27);
  recovery.has("\"waitSeconds\":600");
  recovery.has("\"recoveryAttempts\":2");
  const auto rebootOffPackets = packets.size();
  AcController offBoot(23);
  offBoot.begin(beforeIr, afterIr);
  String offState;
  offBoot.appendJson(offState);
  assert(offState.find("\"enabled\":false") != String::npos);
  assert(offState.find("\"power\":false") != String::npos);
  assert(packets.size() == rebootOffPackets);
  recovery.command("off");
  const auto cancelledPackets = packets.size();
  recovery.read(27, 1800000);
  assert(packets.size() == cancelledPackets);
  recovery.has("\"power\":false");

  Rig sensorLoss;
  sensorLoss.command("autocool on");
  sensorLoss.failCooling();
  sensorLoss.reading.status = TemperatureReading::Status::Unavailable;
  sensorLoss.read(27);
  sensorLoss.has("\"cancelled\":true");
  sensorLoss.has("\"enabled\":false");
  sensorLoss.reading.status = TemperatureReading::Status::Ready;
  sensorLoss.read(27, 1800000);
  sensorLoss.has("\"power\":false");

  Rig storageFailure;
  fakeStorage.fail = true;
  storageFailure.command("autocool on");
  storageFailure.has("\"saved\":false");

  // Automatic signals are identical at 0, 2 and 4 seconds; polling/duplicate
  // samples cause neither extra packets nor extra flash writes.
  Rig retries;
  retries.command("autocool on");
  retries.read(27);
  const auto firstBurst = packets.size();
  const auto repeatedPacket = packets.back();
  const auto retryWrites = fakeStorage.writes;
  retries.has("\"irRetriesRemaining\":2");
  retries.command("state");  // Status reads must not cancel delivery attempts.
  retries.read(27, 1999);
  assert(packets.size() == firstBurst);
  retries.read(27, 1);
  assert(packets.size() == firstBurst + 1 && packets.back() == repeatedPacket);
  retries.has("\"irRetriesRemaining\":1");
  retries.read(27, 1999);
  assert(packets.size() == firstBurst + 1);
  retries.read(27, 1);
  assert(packets.size() == firstBurst + 2 && packets.back() == repeatedPacket);
  retries.has("\"irRetriesRemaining\":0");
  retries.read(27, 10000);
  assert(packets.size() == firstBurst + 2 && fakeStorage.writes == retryWrites);

  // A later settings edit supersedes old packets. Manual commands remain
  // single sends, including an explicit Off during pending Power On retries.
  Rig superseded;
  superseded.command("autocool on");
  superseded.read(27);
  superseded.command("temp 21");
  const auto editedPackets = packets.size();
  superseded.read(27, 2000);
  assert(packets.size() == editedPackets);
  superseded.has("\"irRetriesRemaining\":0");

  Rig powerRetries;
  powerRetries.command("autocool on");
  powerRetries.failCooling();
  const auto offBurst = packets.size();
  const auto offPacket = packets.back();
  powerRetries.read(27, 2000);
  powerRetries.read(27, 2000);
  assert(packets.size() == offBurst + 2);
  assert(packets[offBurst] == offPacket && packets[offBurst + 1] == offPacket);
  powerRetries.read(27, 296000);
  const auto onBurst = packets.size();
  const auto originalOn = packets[onBurst - 2];
  const auto originalTarget = packets[onBurst - 1];
  const auto onWrites = fakeStorage.writes;
  powerRetries.read(27, 2000);
  assert(packets.size() == onBurst + 2);
  assert(packets[onBurst] == originalOn && packets[onBurst + 1] == originalTarget);
  powerRetries.read(27, 2000);
  assert(packets.size() == onBurst + 4);
  assert(packets[onBurst + 2] == originalOn && packets[onBurst + 3] == originalTarget);
  assert(fakeStorage.writes == onWrites);
  powerRetries.has("\"temperature\":20");
  powerRetries.has("\"turbo\":false");  // Power sequence completes first.
  powerRetries.read(27, 5000);
  powerRetries.has("\"turbo\":true");

  Rig cancelOn;
  cancelOn.command("autocool on");
  cancelOn.failCooling();
  cancelOn.read(27, 300000);
  cancelOn.command("off");
  const auto stoppedOn = packets.size();
  cancelOn.read(27, 10000);
  assert(packets.size() == stoppedOn);
  cancelOn.has("\"power\":false");
  cancelOn.has("\"irRetriesRemaining\":0");

  // A lost sensor cancels pending restart signals even after the initial On.
  Rig cancelSensor;
  cancelSensor.command("autocool on");
  cancelSensor.failCooling();
  cancelSensor.read(27, 300000);
  const auto beforeLoss = packets.size();
  cancelSensor.reading.status = TemperatureReading::Status::Unavailable;
  cancelSensor.read(27, 2000);
  assert(packets.size() == beforeLoss);
  cancelSensor.has("\"irRetriesRemaining\":0");

  // Repeated Turbo packets cannot extend the original twenty-minute limit.
  Rig timerRetries;
  timerRetries.command("autocool on");
  timerRetries.read(22.8f);
  for (int i = 0; i < 240; ++i) timerRetries.read(22.8f - i / 300.0f);
  timerRetries.has("\"turbo\":false");
  timerRetries.has("\"turboCooldownSeconds\":300");
  const auto turboOff = packets.back();
  const auto offTurboCount = packets.size();
  timerRetries.read(22, 2000);
  timerRetries.read(22, 2000);
  assert(packets.size() == offTurboCount + 2 && packets.back() == turboOff);

  // A stalled loop sends one burst at a time, and the retry clock wraps safely.
  Rig stalled;
  stalled.command("autocool on");
  stalled.read(27);
  const auto stallPackets = packets.size();
  stalled.read(27, 10000);
  assert(packets.size() == stallPackets + 1);
  stalled.read(27, 0);
  assert(packets.size() == stallPackets + 1);
  stalled.read(27, 2000);
  assert(packets.size() == stallPackets + 2);
  Rig wrapRetries;
  clockMs = UINT32_MAX - 6000;
  wrapRetries.command("autocool on");
  wrapRetries.read(27);
  const auto wrapPackets = packets.size();
  wrapRetries.read(27, 1999);
  assert(packets.size() == wrapPackets);
  wrapRetries.read(27, 1);
  wrapRetries.read(27, 2000);
  assert(packets.size() == wrapPackets + 2);

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
  std::puts("AcController host checks passed: Turbo, recovery, timed packet repeats, cancellation, IR hooks, NVS, reboot, rollover.");
}
