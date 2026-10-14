#ifndef ACTIVITY_LED_H_
#define ACTIVITY_LED_H_

#include <cstdint>

// Independent of power management. BLE/IR activity overrides Auto-Cool's
// one-second on / one-second off heartbeat without restarting its phase.
class ActivityLed {
 public:
  void acCommand(uint32_t now) { lastAcCommand_ = now; haveAcCommand_ = true; }
  bool on(uint32_t now, bool bleActive, bool autoCoolTurboActive) const {
    if (bleActive || (haveAcCommand_ && now - lastAcCommand_ < 500)) return true;
    return autoCoolTurboActive && (now / 1000) % 2 == 0;
  }

 private:
  uint32_t lastAcCommand_ = 0;
  bool haveAcCommand_ = false;
};

#endif
