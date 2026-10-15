#include "temperature_history_api.h"
#include <cassert>
#include <cstring>
#include <string>

int main() {
  using namespace TemperatureHistoryApi;
  uint64_t number;
  assert(parseUnsigned("0", kMaxCursor, number) && number == 0);
  assert(parseUnsigned("9007199254740991", kMaxCursor, number) && number == kMaxCursor);
  for (const char* bad : {"", "-1", "+1", "1.2", " 1", "128x",
                         "9007199254740992", "184467440737095516160"})
    assert(!parseUnsigned(bad, kMaxCursor, number));
  assert(!parseUnsigned("129", 128, number));
  assert(!parseUnsigned("9", 1, number));
  TemperatureHistory history;
  char out[kResponseSize];
  auto page = [&](uint32_t now, uint64_t cursor, bool reset, uint16_t limit = kPageSize) {
    const size_t size = formatPage(history, now, "1234abcd", cursor, reset, limit, out);
    assert(size == std::strlen(out) && size < sizeof(out));
    return std::string(out);
  };
  assert(page(0, 0, true).find("\"samples\":[]}") != std::string::npos);
  for (uint32_t i = 0; i < TemperatureHistory::kCapacity; ++i)
    history.record(i * 5, true, i % 2 ? -55 : 125);
  assert(history.first() == 0 && history.end() == 17280);
  uint64_t cursor = 0;
  unsigned pages = 0;
  while (cursor < history.end()) {
    const std::string result = page(86400, cursor, false);
    const uint64_t next = cursor + (history.end() - cursor < kPageSize ? history.end() - cursor : kPageSize);
    assert(result.find("\"start\":" + std::to_string(cursor) + ",\"next\":" + std::to_string(next)) != std::string::npos);
    assert(result.find("\"age\":" + std::to_string(5 + (17280 - next) * 5)) != std::string::npos);
    assert(result.find("\"reset\":false") != std::string::npos);
    cursor = next;
    ++pages;
  }
  assert(pages == 135);
  history.record(86400, true, -0.01f);
  assert(history.first() == 1 && history.end() == 17281);
  assert(page(86402, cursor, false).find("\"samples\":[-0.01]}") != std::string::npos);
  assert(page(86402, cursor + 1, false).find("\"samples\":[]}") != std::string::npos);
  assert(page(86402, 0, false).find("\"start\":1,") != std::string::npos);
  assert(page(86402, 0, false).find("\"reset\":true") != std::string::npos);
  assert(page(86402, 999999, false).find("\"reset\":true") != std::string::npos);
  // Reboot/session reset ignores an otherwise valid cursor; one-sample pages work.
  assert(page(86402, cursor, true, 1).find("\"next\":2,") != std::string::npos);
  history.record(300000, false, 0);
  assert(history.end() == 60001 && history.first() == 42721);
  assert(page(300000, 0, true, 1).find("\"samples\":[null]}") != std::string::npos);
}
