#include "auto_cool.h"
#include <cassert>
#include <limits>

using Action = AutoCool::Action;
struct Rig {
  AutoCool control;
  uint32_t now = 0, sample = 0;
  bool power = true, turbo = false, coolMode = true, turboAllowed = true;
  unsigned cycles = 0;
  Rig() { control.enable(24); }
  Action read(float temperature, uint32_t elapsed = 5000, bool available = true) {
    now += elapsed;
    sample += elapsed / 1000;
    const auto action = control.update(now, sample, available, temperature,
        power, coolMode, turbo, turboAllowed);
    if (action == Action::TurboOn) turbo = true;
    if (action == Action::TurboOff) turbo = false;
    if (action == Action::PowerOff) { power = false; ++cycles; }
    if (action == Action::PowerOn) power = true;
    return action;
  }
  void trigger() {
    read(27);
    for (int i = 1; i < 240; ++i) assert(read(27) != Action::PowerOff);
    assert(read(27) == Action::PowerOff);
    assert(control.waiting() && !power);
  }
  void restart() {
    const auto wait = control.waitSeconds(now);
    assert(read(27, wait * 1000 - 1) == Action::None);
    assert(control.waitSeconds(now) == 1);
    assert(read(27, 1) == Action::PowerOn);
    assert(power && !control.waiting());
  }
};

int main() {
  Rig thresholds;
  assert(thresholds.read(25.5f) == Action::None);
  assert(thresholds.read(25.5625f) == Action::TurboOn);
  assert(thresholds.read(23.5f) == Action::None);
  assert(thresholds.read(23.4375f) == Action::TurboOff);
  assert(thresholds.control.target() == 24);

  // Flat/rising/noisy hot rooms can retry indefinitely, with a saturated wait.
  Rig repeat;
  repeat.trigger();
  const uint32_t delays[] = {300, 600, 1200, 1800, 1800, 1800, 1800};
  for (unsigned i = 0; i < 7; ++i) {
    assert(repeat.control.recoveryAttempts() == i + 1);
    assert(repeat.control.waitSeconds(repeat.now) == delays[i]);
    repeat.restart();
    const auto onAt = repeat.now;
    // No second Off during the complete twenty-minute trial.
    for (int step = 1; step < 240; ++step)
      assert(repeat.read(27 + (step % 2) * 0.0625f) != Action::PowerOff);
    assert(repeat.read(27) == Action::PowerOff);
    assert(repeat.now - onAt == 1200000);
  }
  assert(repeat.cycles == 8);

  // A full 0.25 C drop is enough; a smaller drop is insufficient. Settling
  // temperatures are excluded, and the two endpoint averages reject spikes.
  for (int kind = 0; kind < 4; ++kind) {
    Rig drop;
    drop.read(28);
    for (int i = 1; i <= 240; ++i) {
      float temperature = i < 60 ? 28 : i < 120 ? 27 :
          kind == 0 ? 26.75f : kind == 1 ? 26.8125f : 27;
      if (kind == 2 && i == 240) temperature = 26; // One cold spike isn't success.
      if (kind == 3 && i == 60) temperature = 26; // One baseline dip isn't failure.
      const auto action = drop.read(temperature);
      if (i < 240) assert(action != Action::PowerOff);
    }
    assert(drop.cycles == (kind == 0 ? 0U : 1U));
  }

  // Gaps, invalid data, target edits, and crossing the upper hysteresis
  // boundary all restart a full trial; stale duplicates cannot fill averages.
  for (int kind = 0; kind < 6; ++kind) {
    Rig gap;
    gap.read(27);
    for (int i = 0; i < 230; ++i) gap.read(27);
    if (kind == 0) gap.read(std::numeric_limits<float>::quiet_NaN());
    if (kind == 1) gap.read(27, 15000);
    if (kind == 2) gap.control.setTarget(25);
    if (kind == 3) gap.read(25.5f);
    if (kind == 4) gap.read(27, 5000, false);
    if (kind == 5) {
      for (int i = 0; i < 1000; ++i)
        assert(gap.control.update(gap.now, gap.sample, true, 27,
            true, true, true) == Action::None);
      gap.read(27, 15000);
    }
    for (int i = 0; i < 239; ++i) assert(gap.read(27) != Action::PowerOff);
    assert(!gap.control.waiting());
  }

  // Successful cooling resets retry escalation only after sustained evidence.
  Rig success;
  success.trigger();
  success.restart();
  success.read(23.9375f);
  assert(success.control.recoveryAttempts() == 1);
  success.read(24);  // Equality interrupts the strictly-below-target interval.
  success.read(23.9375f);
  for (int i = 0; i < 59; ++i) success.read(23.9375f);
  assert(success.control.recoveryAttempts() == 1);
  success.read(23.9375f);
  assert(success.control.recoveryAttempts() == 0);
  success.trigger();
  assert(success.control.waitSeconds(success.now) == 300);

  Rig cooling;
  cooling.trigger();
  cooling.restart();
  for (int i = 1; i <= 240; ++i) cooling.read(i < 120 ? 27 : 26.5f);
  assert(cooling.control.recoveryAttempts() == 0 && cooling.power);

  // Turbo expiry/cooldown do not reset the cooling trial.
  Rig normal;
  normal.read(27);
  for (int i = 0; i < 150; ++i) normal.read(27);
  normal.turbo = false;
  normal.turboAllowed = false;
  for (int i = 151; i < 240; ++i) assert(normal.read(27) == Action::None);
  assert(normal.read(27) == Action::PowerOff);

  // Manual changes preserve backoff and cancellation never queues a restart.
  Rig cancel;
  cancel.trigger();
  cancel.control.disable();
  assert(cancel.read(27, 1800000) == Action::None && !cancel.power);
  assert(cancel.control.recoveryAttempts() == 1);
  cancel.power = true;
  cancel.control.enable(24);
  cancel.trigger();
  assert(cancel.control.waitSeconds(cancel.now) == 600);

  Rig lost;
  lost.trigger();
  lost.read(27, 5000, false);
  assert(lost.control.cancelled() && !lost.control.enabled());
  assert(lost.read(27, 1800000) == Action::None && !lost.power);
  Rig invalid;
  invalid.trigger();
  invalid.read(std::numeric_limits<float>::infinity());
  assert(invalid.control.cancelled());

  Rig missing;
  missing.read(27);
  assert(missing.read(27, 5000, false) == Action::TurboOff);
  assert(missing.control.enabled() && missing.power && !missing.turbo);
  Rig off;
  off.power = false;
  assert(off.read(27) == Action::None && !off.control.enabled());
  Rig fan;
  fan.coolMode = false;
  assert(fan.read(27) == Action::None && !fan.control.enabled());

  // Trial, wait and below-target success timers survive unsigned rollover.
  Rig rollover;
  rollover.now = UINT32_MAX - 600000;
  rollover.trigger();
  // Start a new recovery whose Off wait crosses the wrap.
  Rig wrapped;
  wrapped.now = UINT32_MAX - 1300000;
  wrapped.trigger();
  wrapped.restart();
  Rig successWrap;
  successWrap.now = UINT32_MAX - 1600000;
  successWrap.trigger();
  successWrap.restart();
  successWrap.read(23);
  for (int i = 0; i < 60; ++i) successWrap.read(23);
  assert(successWrap.control.recoveryAttempts() == 0);

  // Reboot restores settings, never recovery counters or pending restarts.
  AutoCool restored;
  restored.enable(24);
  assert(!restored.waiting() && restored.recoveryAttempts() == 0);
  assert(restored.update(0, 0, true, 27, false, true, true) == Action::None);
}
