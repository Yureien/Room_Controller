#pragma once
#include <cstdint>

// Tracks transmitted Turbo sessions, independently of sensor availability.
class TurboTimer {
 public:
  static constexpr uint32_t kDurationMs = 20 * 60 * 1000;
  static constexpr uint32_t kCooldownMs = 5 * 60 * 1000;

  void sent(uint32_t now, bool power, bool turbo) {
    if (!power || !turbo) {
      running_ = false;
    } else if (!running_) {
      started_ = now;
      running_ = true;
    }
  }
  bool expire(uint32_t now) {
    if (!running_ || now - started_ < kDurationMs) return false;
    running_ = false;
    coolingDown_ = true;
    expired_ = now;
    return true;
  }
  uint32_t cooldownSeconds(uint32_t now) {
    const uint32_t elapsed = now - expired_;
    if (elapsed >= kCooldownMs) coolingDown_ = false;
    return coolingDown_ ? (kCooldownMs - elapsed + 999) / 1000 : 0;
  }

 private:
  uint32_t started_ = 0, expired_ = 0;
  bool running_ = false, coolingDown_ = false;
};
