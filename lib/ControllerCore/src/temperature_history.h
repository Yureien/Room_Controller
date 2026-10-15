#ifndef TEMPERATURE_HISTORY_H_
#define TEMPERATURE_HISTORY_H_

#include <cmath>
#include <cstddef>
#include <cstdint>

// Fixed RAM budget, no flash writes or per-sample allocations. Timestamps are
// implicit at a fixed cadence; missing readings occupy a slot to preserve gaps.
class TemperatureHistory {
 public:
  TemperatureHistory() = default;
  TemperatureHistory(const TemperatureHistory&) = delete;
  TemperatureHistory& operator=(const TemperatureHistory&) = delete;
  static constexpr uint32_t kIntervalSeconds = 5;
  static constexpr uint16_t kCapacity = 24 * 60 * 60 / kIntervalSeconds;
  static constexpr int16_t kMissing = INT16_MIN;

  void record(uint32_t now, bool available, float celsius) {
    const uint32_t steps = count_ ? (now - lastSample_) / kIntervalSeconds : 0;
    if (count_ && !steps) return;
    const int16_t value = available && std::isfinite(celsius) &&
        celsius >= -55 && celsius <= 125 ?
        static_cast<int16_t>(std::lround(celsius * 100)) : kMissing;
    if (!count_) {
      append(value);
      lastSample_ = now;
      return;
    }
    // Bound catch-up work even after a very long pause.
    if (steps >= kCapacity) { count_ = 0; next_ = 0; }
    const uint32_t retained = steps < kCapacity ? steps : kCapacity;
    // Cursors count every interval, including gaps that have already expired.
    end_ += steps - retained;
    for (uint32_t i = 1; i < retained; i++) append(kMissing);
    append(value);
    lastSample_ += steps * kIntervalSeconds;
  }

  uint16_t count() const { return count_; }
  uint64_t end() const { return end_; }
  uint64_t first() const { return end_ - count_; }
  uint32_t age(uint32_t now) const { return count_ ? now - lastSample_ : 0; }
  int16_t sample(uint16_t index) const {
    return samples_[(next_ + kCapacity - count_ + index) % kCapacity];
  }

 private:
  int16_t samples_[kCapacity]{};
  uint16_t next_ = 0, count_ = 0;
  uint32_t lastSample_ = 0;
  uint64_t end_ = 0;
  void append(int16_t value) {
    samples_[next_] = value;
    next_ = (next_ + 1) % kCapacity;
    if (count_ < kCapacity) count_++;
    end_++;
  }
};
static_assert(sizeof(TemperatureHistory) <= 34 * 1024, "History RAM budget exceeded");

#endif
