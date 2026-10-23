#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct StorageFake {
  std::map<std::string, std::vector<uint8_t>> values;
  unsigned writes = 0;
  bool fail = false;
};
extern StorageFake fakeStorage;
class Preferences {
 public:
  bool begin(const char* name, bool) { return std::string(name) == "ac-controller"; }
  size_t getBytesLength(const char* key) { return fakeStorage.values[key].size(); }
  size_t getBytes(const char* key, void* out, size_t size) {
    const auto& value = fakeStorage.values[key];
    if (value.size() != size) return 0;
    std::memcpy(out, value.data(), size);
    return size;
  }
  size_t putBytes(const char* key, const void* data, size_t size) {
    if (fakeStorage.fail) return 0;
    const auto* first = static_cast<const uint8_t*>(data);
    fakeStorage.values[key] = std::vector<uint8_t>(first, first + size);
    ++fakeStorage.writes;
    return size;
  }
};
