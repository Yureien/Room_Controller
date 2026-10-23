#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <type_traits>
#define F(value) value
#define HEX 16
class String : public std::string {
 public:
  using std::string::string;
  using std::string::operator=;
  String() = default;
  String(const std::string& value) : std::string(value) {}
  template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  explicit String(T value) : std::string(std::to_string(value)) {}
  void trim() {
    const auto first = find_first_not_of(" \t\r\n");
    *this = first == npos ? "" : substr(first, find_last_not_of(" \t\r\n") - first + 1);
  }
  void toLowerCase() {
    std::transform(begin(), end(), begin(), [](unsigned char c) { return std::tolower(c); });
  }
  int indexOf(char c) const { const auto at = find(c); return at == npos ? -1 : int(at); }
  String substring(size_t first) const { return substr(first); }
  String substring(size_t first, size_t end) const { return substr(first, end - first); }
  long toInt() const { return std::strtol(c_str(), nullptr, 10); }
  void replace(const char* from, const char* to) {
    size_t at = 0;
    while ((at = find(from, at)) != npos) {
      std::string::replace(at, std::string(from).size(), to);
      at += std::string(to).size();
    }
  }
};
struct SerialFake {
  template<class... T> void print(T...) {}
  template<class... T> void println(T...) {}
  template<class... T> void printf(T...) {}
};
extern SerialFake Serial;
extern uint32_t clockMs;
inline uint32_t millis() { return clockMs; }
