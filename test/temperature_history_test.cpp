#include "temperature_history.h"
#include <cassert>
#include <limits>

int main() {
  TemperatureHistory history;
  assert(history.count() == 0 && history.age(100) == 0);
  history.record(10, true, 25.125f);
  assert(history.count() == 1 && history.sample(0) == 2513);
  history.record(69, true, 30);
  assert(history.count() == 1 && history.age(69) == 59);
  history.record(70, true, -10.125f);
  assert(history.count() == 2 && history.sample(1) == -1013);
  history.record(130, false, 25);
  history.record(190, true, std::numeric_limits<float>::quiet_NaN());
  history.record(250, true, 126);
  for (unsigned i = 2; i < 5; i++) assert(history.sample(i) == TemperatureHistory::kMissing);

  // Delayed service preserves missing minutes and the original cadence.
  history.record(440, true, 20);
  assert(history.count() == 8 && history.age(440) == 10);
  assert(history.sample(5) == TemperatureHistory::kMissing);
  assert(history.sample(6) == TemperatureHistory::kMissing);
  assert(history.sample(7) == 2000);

  TemperatureHistory full;
  for (uint32_t i = 0; i < 2000; i++) full.record(i * 60, true, i % 100);
  assert(full.count() == TemperatureHistory::kCapacity);
  for (uint16_t i = 0; i < full.count(); i++)
    assert(full.sample(i) == ((i + 560) % 100) * 100);

  // An outage longer than retention never performs unbounded catch-up work.
  full.record(300000, true, 21);
  assert(full.count() == TemperatureHistory::kCapacity);
  for (uint16_t i = 0; i < full.count() - 1; i++) assert(full.sample(i) == TemperatureHistory::kMissing);
  assert(full.sample(full.count() - 1) == 2100);

  TemperatureHistory rollover;
  rollover.record(UINT32_MAX - 20, true, -55);
  rollover.record(39, true, 125);
  assert(rollover.count() == 2 && rollover.sample(0) == -5500 && rollover.sample(1) == 12500);
  assert(rollover.age(44) == 5);
}
