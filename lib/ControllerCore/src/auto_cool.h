#ifndef AUTO_COOL_H_
#define AUTO_COOL_H_

#include <cmath>
#include <cstdint>

// Decisions only: the caller sends IR. No allocation, flash writes, or delays.
class AutoCool {
 public:
  enum class Action { None, TurboOn, TurboOff, PowerOff, PowerOn };
  static constexpr uint32_t kRecoveryWaitMs = 120000;
  static constexpr uint32_t kTrendMs = 300000;

  void enable(uint8_t target) {
    enabled_ = true;
    target_ = target;
    waiting_ = cancelled_ = haveSample_ = false;
    resetTrend();
  }
  void disable() {
    enabled_ = waiting_ = haveSample_ = cancelled_ = false;
    resetTrend();
    // Deliberately preserve the one-recovery latch across manual commands.
  }
  void setTarget(uint8_t target) { target_ = target; resetTrend(); }
  bool enabled() const { return enabled_; }
  bool waiting() const { return waiting_; }
  bool recoveryUsed() const { return recoveryUsed_; }
  bool cancelled() const { return cancelled_; }
  uint8_t target() const { return target_; }
  uint32_t waitSeconds(uint32_t now) const {
    const uint32_t elapsed = now - offStarted_;
    return waiting_ && elapsed < kRecoveryWaitMs ?
        (kRecoveryWaitMs - elapsed + 999) / 1000 : 0;
  }

  Action update(uint32_t now, uint32_t sampleTime, bool available,
                float celsius, bool power, bool coolMode, bool turbo,
                bool turboAllowed = true) {
    if (available && std::isfinite(celsius) && celsius >= -55 &&
        celsius <= 125 && celsius < target_) recoveryUsed_ = false;
    if (!enabled_) return Action::None;
    if (!coolMode || (!waiting_ && !power) || (waiting_ && power)) {
      disable();
      return Action::None;
    }
    if (!available || !std::isfinite(celsius) || celsius < -55 || celsius > 125) {
      resetTrend();
      haveSample_ = false;
      if (waiting_) { disable(); cancelled_ = true; return Action::None; }
      return turbo ? Action::TurboOff : Action::None;
    }
    const bool newSample = !haveSample_ || sampleTime != lastSample_;
    if (newSample) {
      if (haveSample_ && sampleTime - lastSample_ > 7) resetTrend();
      lastSample_ = sampleTime;
      haveSample_ = true;
      if (celsius < target_) recoveryUsed_ = false;
    }
    if (waiting_) {
      if (now - offStarted_ < kRecoveryWaitMs) return Action::None;
      waiting_ = false;
      resetTrend();
      return Action::PowerOn;
    }
    if (!newSample) return Action::None;
    if (celsius < target_ - 0.5f) {
      resetTrend();
      return turbo ? Action::TurboOff : Action::None;
    }
    if (celsius <= target_ + 1.5f) {
      resetTrend();
      return Action::None;
    }
    if (!turbo) {
      resetTrend();
      if (!turboAllowed) return Action::None;
      addSample(now, celsius);
      return Action::TurboOn;
    }
    addSample(now, celsius);
    if (recoveryUsed_ || count_ < kSamples || now - trendStarted_ < kTrendMs)
      return Action::None;
    // Compare 30-second averages at either end of the five-minute window.
    // Two DS18B20 resolution steps (0.125 C) avoid reacting to one-step noise.
    int32_t first = 0, last = 0, previous = 0;
    for (uint8_t i = 0; i < 6; ++i) {
      first += samples_[(next_ + i) % kSamples];
      last += samples_[(next_ + kSamples - 6 + i) % kSamples];
      previous += samples_[(next_ + kSamples - 12 + i) % kSamples];
    }
    if (last - first < 75 || last <= previous) return Action::None;
    recoveryUsed_ = waiting_ = true;
    offStarted_ = now;
    resetTrend();
    return Action::PowerOff;
  }

 private:
  static constexpr uint8_t kSamples = 61;  // Five minutes at five-second cadence.
  int16_t samples_[kSamples]{};
  uint32_t offStarted_ = 0, trendStarted_ = 0, lastSample_ = 0;
  uint8_t next_ = 0, count_ = 0, target_ = 24;
  bool enabled_ = false, waiting_ = false, recoveryUsed_ = false;
  bool haveSample_ = false, cancelled_ = false;
  void resetTrend() { count_ = next_ = 0; }
  void addSample(uint32_t now, float celsius) {
    if (!count_) trendStarted_ = now;
    samples_[next_] = static_cast<int16_t>(std::lround(celsius * 100));
    next_ = (next_ + 1) % kSamples;
    if (count_ < kSamples) ++count_;
  }
};
static_assert(sizeof(AutoCool) <= 160, "Auto-Cool RAM budget exceeded");

#endif
