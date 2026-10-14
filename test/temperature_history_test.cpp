#include "temperature_history.h"
#include <cassert>
#include <limits>

int main() {
  TemperatureHistory history;
  assert(history.count() == 0 && history.age(100) == 0);
  history.record(10, true, 25.125f);
  assert(history.count() == 1 && history.sample(0) == 2513);
  history.record(14, true, 30);
  assert(history.count() == 1 && history.age(14) == 4);
  history.record(15, true, -10.125f);
  assert(history.count() == 2 && history.sample(1) == -1013);
  history.record(20, false, 25);
  history.record(25, true, std::numeric_limits<float>::quiet_NaN());
  history.record(30, true, 126);
  for (unsigned i = 2; i < 5; i++) assert(history.sample(i) == TemperatureHistory::kMissing);

  // Delayed service preserves missing intervals and the original cadence.
  history.record(47, true, 20);
  assert(history.count() == 8 && history.age(47) == 2);
  assert(history.sample(5) == TemperatureHistory::kMissing);
  assert(history.sample(6) == TemperatureHistory::kMissing);
  assert(history.sample(7) == 2000);

  TemperatureHistory full;
  static_assert(TemperatureHistory::kCapacity * TemperatureHistory::kIntervalSeconds ==
                24 * 60 * 60, "Must retain 24 hours");
  const uint32_t total = TemperatureHistory::kCapacity + 560;
  for (uint32_t i = 0; i < total; i++) full.record(i * 5, true, i % 100);
  assert(full.count() == TemperatureHistory::kCapacity);
  for (uint16_t i = 0; i < full.count(); i++)
    assert(full.sample(i) == ((i + 560) % 100) * 100);

  // An outage longer than retention never performs unbounded catch-up work.
  full.record(300000, true, 21);
  assert(full.count() == TemperatureHistory::kCapacity);
  for (uint16_t i = 0; i < full.count() - 1; i++) assert(full.sample(i) == TemperatureHistory::kMissing);
  assert(full.sample(full.count() - 1) == 2100);

  TemperatureHistory rollover;
  rollover.record(UINT32_MAX - 2, true, -55);
  rollover.record(2, true, 125);
  assert(rollover.count() == 2 && rollover.sample(0) == -5500 && rollover.sample(1) == 12500);
  assert(rollover.age(4) == 2);
}
