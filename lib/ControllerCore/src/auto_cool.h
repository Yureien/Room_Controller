#ifndef AUTO_COOL_H_
#define AUTO_COOL_H_

#include <cmath>
#include <cstdint>

// Decisions only: the caller sends IR. No allocation, flash writes, or delays.
class AutoCool {
 public:
  enum class Action { None, TurboOn, TurboOff, PowerOff, PowerOn };
  static constexpr uint32_t kSettleMs = 5 * 60 * 1000;
  static constexpr uint32_t kObserveMs = 15 * 60 * 1000;
  static constexpr uint32_t kSuccessMs = 5 * 60 * 1000;

  void enable(uint8_t target) {
    enabled_ = true;
    target_ = target;
    waiting_ = cancelled_ = haveSample_ = haveSuccess_ = false;
    resetObservation();
  }
  void disable() {
    enabled_ = waiting_ = haveSample_ = cancelled_ = haveSuccess_ = false;
    resetObservation();
    // Manual commands cannot bypass accumulated retry backoff.
  }
  void setTarget(uint8_t target) {
    target_ = target;
    haveSuccess_ = false;
    resetObservation();
  }
  bool enabled() const { return enabled_; }
  bool waiting() const { return waiting_; }
  bool recoveryUsed() const { return attempts_ != 0; }
  uint32_t recoveryAttempts() const { return attempts_; }
  bool cancelled() const { return cancelled_; }
  uint8_t target() const { return target_; }
  uint32_t recoveryBackoffSeconds() const {
    const uint32_t attempt = waiting_ || attempts_ == UINT32_MAX ? attempts_ : attempts_ + 1;
    return waitMs(attempt) / 1000;
  }
  uint32_t waitSeconds(uint32_t now) const {
    const uint32_t elapsed = now - offStarted_;
    const uint32_t duration = waitMs(attempts_);
    return waiting_ && elapsed < duration ? (duration - elapsed + 999) / 1000 : 0;
  }

  Action update(uint32_t now, uint32_t sampleTime, bool available,
                float celsius, bool power, bool coolMode, bool turbo,
                bool turboAllowed = true) {
    if (!enabled_) return Action::None;
    if (!coolMode || (!waiting_ && !power) || (waiting_ && power)) {
      disable();
      return Action::None;
    }
    if (!available || !std::isfinite(celsius) || celsius < -55 || celsius > 125) {
      resetObservation();
      haveSample_ = haveSuccess_ = false;
      if (waiting_) { disable(); cancelled_ = true; return Action::None; }
      return turbo ? Action::TurboOff : Action::None;
    }
    const bool newSample = !haveSample_ || sampleTime != lastSample_;
    if (newSample) {
      if (haveSample_ && sampleTime - lastSample_ > 7) {
        resetObservation();
        haveSuccess_ = false;
      }
      lastSample_ = sampleTime;
      haveSample_ = true;
    }
    if (waiting_) {
      // The Off interval cannot count as successful cooling.
      if (now - offStarted_ < waitMs(attempts_)) return Action::None;
      waiting_ = false;
      startSettling(now);
      return Action::PowerOn;
    }
    if (!newSample) return Action::None;
    if (celsius < target_) {
      if (!haveSuccess_) { successStarted_ = now; haveSuccess_ = true; }
      if (now - successStarted_ >= kSuccessMs) attempts_ = 0;
    } else {
      haveSuccess_ = false;
    }
    if (celsius <= target_ + 1.5f) {
      resetObservation();
      return celsius < target_ - 0.5f && turbo ? Action::TurboOff : Action::None;
    }

    // Recovery observes normal cooling as well as Turbo, including its cooldown.
    if (phase_ == Phase::Idle) startSettling(now);
    if (phase_ == Phase::Settling && now - phaseStarted_ >= kSettleMs) {
      phase_ = Phase::Measuring;
      phaseStarted_ = now;
    }
    if (phase_ == Phase::Measuring) {
      addSample(celsius);
      if (count_ == kAverageSamples && now - phaseStarted_ >= kObserveMs) {
        if (firstSum_ - lastSum_ < kMinimumDropSum) {
          if (attempts_ != UINT32_MAX) ++attempts_;
          waiting_ = true;
          offStarted_ = now;
          haveSuccess_ = false;
          resetObservation();
          return Action::PowerOff;
        }
        // A full successful measurement is sustained cooling: reset backoff
        // and begin another measurement without imposing a new startup grace.
        attempts_ = 0;
        resetObservation();
        phase_ = Phase::Measuring;
        phaseStarted_ = now;
        addSample(celsius);
      }
    }
    return !turbo && turboAllowed ? Action::TurboOn : Action::None;
  }

 private:
  enum class Phase : uint8_t { Idle, Settling, Measuring };
  static constexpr uint8_t kAverageSamples = 6;  // 30s at five-second cadence.
  static constexpr int32_t kMinimumDropSum = 150;  // 6 * 0.25 C * 100.
  int16_t samples_[kAverageSamples]{};
  int32_t firstSum_ = 0, lastSum_ = 0;
  uint32_t offStarted_ = 0, phaseStarted_ = 0, lastSample_ = 0;
  uint32_t successStarted_ = 0, attempts_ = 0;
  uint8_t next_ = 0, count_ = 0, target_ = 24;
  Phase phase_ = Phase::Idle;
  bool enabled_ = false, waiting_ = false, cancelled_ = false;
  bool haveSample_ = false, haveSuccess_ = false;
  static uint32_t waitMs(uint32_t attempt) {
    return attempt <= 1 ? 300000 : attempt == 2 ? 600000 :
        attempt == 3 ? 1200000 : 1800000;
  }
  void resetObservation() {
    phase_ = Phase::Idle;
    count_ = next_ = 0;
    firstSum_ = lastSum_ = 0;
  }
  void startSettling(uint32_t now) {
    resetObservation();
    phase_ = Phase::Settling;
    phaseStarted_ = now;
  }
  void addSample(float celsius) {
    const int16_t value = static_cast<int16_t>(std::lround(celsius * 100));
    if (count_ < kAverageSamples) {
      firstSum_ += value;
      ++count_;
    } else {
      lastSum_ -= samples_[next_];
    }
    samples_[next_] = value;
    lastSum_ += value;
    next_ = (next_ + 1) % kAverageSamples;
  }
};
static_assert(sizeof(AutoCool) <= 160, "Auto-Cool RAM budget exceeded");

#endif
