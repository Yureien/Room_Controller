#include "auto_cool.h"
#include <cassert>
#include <limits>

using Action = AutoCool::Action;

struct Rig {
  AutoCool control;
  uint32_t now = 0, sample = 0;
  bool power = true, turbo = false, coolMode = true;
  unsigned cycles = 0;
  Rig() { control.enable(24); }
  Action read(float temperature, uint32_t elapsed = 5000, bool available = true) {
    now += elapsed;
    sample += elapsed / 1000;
    const Action action = control.update(now, sample, available, temperature,
                                         power, coolMode, turbo);
    if (action == Action::TurboOn) turbo = true;
    if (action == Action::TurboOff) turbo = false;
    if (action == Action::PowerOff) { power = false; ++cycles; }
    if (action == Action::PowerOn) power = true;
    return action;
  }
  void trigger() {
    assert(read(26) == Action::TurboOn);
    for (int i = 1; i < 60; ++i)
      assert(read(26 + i / 100.0f) == Action::None);
    assert(read(26.6f) == Action::PowerOff);
    assert(control.waiting() && control.recoveryUsed() && !power);
  }
};

int main() {
  // Strict hysteresis thresholds, independent of Turbo's displayed 16 C.
  Rig thresholds;
  assert(thresholds.read(25.5f) == Action::None);
  assert(thresholds.read(25.5625f) == Action::TurboOn);
  assert(thresholds.control.target() == 24);
  assert(thresholds.read(23.5f) == Action::None);
  assert(thresholds.read(23.4375f) == Action::TurboOff);

  // A hot but flat/cooling room and one-step sensor noise never cycle power.
  for (int kind = 0; kind < 3; ++kind) {
    Rig stable;
    for (int i = 0; i < 200; ++i) {
      const float value = kind == 0 ? 27.0f : kind == 1 ?
          28.0f - i / 200.0f : 27.0f + (i % 2) * 0.0625f;
      stable.read(value);
    }
    assert(stable.cycles == 0 && stable.power);
  }

  // An earlier increase followed by a plateau or falling temperature isn't
  // "still rising" at the end of the five-minute observation window.
  for (int kind = 0; kind < 2; ++kind) {
    Rig settling;
    for (int i = 0; i < 100; ++i) {
      const float value = i < 30 ? 26.0f + i / 30.0f :
          kind == 0 ? 27.0f : 28.0f - i / 100.0f;
      settling.read(value);
    }
    assert(settling.cycles == 0);
  }

  Rig recovery;
  recovery.trigger();
  assert(recovery.control.waitSeconds(recovery.now) == 120);
  for (int i = 0; i < 23; ++i) assert(recovery.read(27) == Action::None);
  assert(recovery.control.waitSeconds(recovery.now) == 5);
  assert(recovery.read(27) == Action::PowerOn);
  assert(recovery.power && !recovery.control.waiting());
  for (int i = 0; i < 130; ++i) recovery.read(27 + i / 100.0f);
  assert(recovery.cycles == 1);  // A failed recovery cannot repeat itself.
  recovery.control.disable();
  recovery.control.enable(24);
  for (int i = 0; i < 65; ++i) recovery.read(28 + i / 100.0f);
  assert(recovery.cycles == 1);  // Button toggles cannot clear the latch.
  recovery.read(24);  // Equal to target must not clear the latch.
  assert(recovery.control.recoveryUsed());
  recovery.read(23.9375f);
  assert(!recovery.control.recoveryUsed());
  recovery.read(23);  // Turn Turbo off before another rising window.
  recovery.trigger();
  assert(recovery.cycles == 2);

  // Manual Off or disabling Auto-Cool cancels an outstanding restart.
  Rig cancel;
  cancel.trigger();
  cancel.control.disable();
  assert(cancel.read(27, 120000) == Action::None && !cancel.power);
  assert(cancel.control.recoveryUsed());
  cancel.read(23);
  assert(!cancel.control.recoveryUsed());  // Cold readings also rearm while disabled.

  // Sensor loss cancels recovery and requires an explicit manual restart.
  Rig lost;
  lost.trigger();
  lost.read(27, 5000, false);
  assert(lost.control.cancelled() && !lost.control.enabled());
  assert(lost.read(27, 120000) == Action::None && !lost.power);

  Rig missing;
  missing.read(26);
  assert(missing.read(26, 5000, false) == Action::TurboOff);
  assert(missing.control.enabled() && missing.power && !missing.turbo);

  // Missing readings, a service stall, and target edits restart trend collection.
  for (int kind = 0; kind < 3; ++kind) {
    Rig gap;
    gap.read(26);
    for (int i = 0; i < 50; ++i) gap.read(26 + i / 100.0f);
    if (kind == 0) gap.read(std::numeric_limits<float>::quiet_NaN());
    else if (kind == 1) gap.read(27, 15000);
    else gap.control.setTarget(25);
    for (int i = 0; i < 20; ++i) gap.read(27 + i / 100.0f);
    assert(gap.cycles == 0);
  }

  // Old readings do not fill the trend window, even if the main loop is fast.
  Rig duplicate;
  duplicate.read(26);
  for (int i = 0; i < 1000; ++i)
    assert(duplicate.control.update(duplicate.now, duplicate.sample, true, 27,
        true, true, true) == Action::None);
  assert(duplicate.cycles == 0);

  Rig off;
  off.power = false;
  assert(off.read(30) == Action::None && !off.control.enabled());
  Rig fan;
  fan.coolMode = false;
  assert(fan.read(30) == Action::None && !fan.control.enabled());

  // Unsigned elapsed-time arithmetic survives millis() wrapping during recovery.
  Rig rollover;
  rollover.now = UINT32_MAX - 330000;
  rollover.trigger();
  for (int i = 0; i < 23; ++i) assert(rollover.read(27) == Action::None);
  assert(rollover.read(27) == Action::PowerOn);

  // Reboot restores only enabled/target; recovery timers and latch start empty.
  Rig reboot;
  reboot.trigger();
  AutoCool restored;
  restored.enable(reboot.control.target());
  assert(restored.enabled() && restored.target() == 24);
  assert(!restored.waiting() && !restored.recoveryUsed());
  assert(restored.update(0, 0, true, 27, false, true, true) == Action::None);
}
