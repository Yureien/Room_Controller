#ifndef TEMPERATURE_HISTORY_API_H_
#define TEMPERATURE_HISTORY_API_H_

#include "temperature_history.h"
#include <cstdio>

namespace TemperatureHistoryApi {
constexpr uint16_t kPageSize = 128;
// Each sample, including its comma, takes at most seven bytes (-55.00).
constexpr size_t kResponseSize = kPageSize * 7 + 320;
constexpr uint64_t kMaxCursor = 9007199254740991ULL;  // JavaScript safe integer.

inline bool parseUnsigned(const char* text, uint64_t maximum, uint64_t& value) {
  if (!text || !*text) return false;
  value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    const unsigned digit = *text - '0';
    if (digit > maximum || value > (maximum - digit) / 10) return false;
    value = value * 10 + digit;
  }
  return true;
}

// Called only from the main loop, so the ring cannot change during a page.
// Age refers to the last returned sample, including during initial backfill.
inline size_t formatPage(const TemperatureHistory& history, uint32_t now,
                         const char (&session)[9], uint64_t cursor, bool reset,
                         uint16_t limit, char (&out)[kResponseSize]) {
  if (cursor < history.first() || cursor > history.end()) reset = true;
  if (reset) cursor = history.first();
  if (!limit || limit > kPageSize) limit = kPageSize;
  const uint16_t count = static_cast<uint16_t>(
      history.end() - cursor < limit ? history.end() - cursor : limit);
  const uint64_t next = cursor + count;
  const uint32_t age = history.age(now) +
      static_cast<uint32_t>(history.end() - next) * TemperatureHistory::kIntervalSeconds;
  size_t used = snprintf(out, sizeof(out),
      "{\"session\":\"%s\",\"interval\":%lu,\"age\":%lu,\"first\":%llu,"
      "\"start\":%llu,\"next\":%llu,\"end\":%llu,\"total\":%u,"
      "\"reset\":%s,\"more\":%s,\"samples\":[",
      session, static_cast<unsigned long>(TemperatureHistory::kIntervalSeconds),
      static_cast<unsigned long>(age), static_cast<unsigned long long>(history.first()),
      static_cast<unsigned long long>(cursor), static_cast<unsigned long long>(next),
      static_cast<unsigned long long>(history.end()), history.count(),
      reset ? "true" : "false", next < history.end() ? "true" : "false");
  for (uint16_t i = 0; i < count; ++i) {
    const int16_t value = history.sample(static_cast<uint16_t>(cursor - history.first() + i));
    if (value == TemperatureHistory::kMissing)
      used += snprintf(out + used, sizeof(out) - used, "%snull", i ? "," : "");
    else {
      // Integer formatting avoids thousands of floating-point conversions.
      const int magnitude = value < 0 ? -value : value;
      used += snprintf(out + used, sizeof(out) - used, "%s%s%d.%02d",
          i ? "," : "", value < 0 ? "-" : "", magnitude / 100, magnitude % 100);
    }
  }
  used += snprintf(out + used, sizeof(out) - used, "]}");
  return used;
}
}  // namespace TemperatureHistoryApi
#endif
