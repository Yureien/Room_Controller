#pragma once
#include <Arduino.h>

inline String jsonString(const String &value) {
  String result = "\"";
  for (size_t i = 0; i < value.length(); i++) {
    const uint8_t c = value[i];
    if (c == '"' || c == '\\') {
      result += '\\';
      result += static_cast<char>(c);
    } else if (c < 32) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      result += escaped;
    } else {
      result += static_cast<char>(c);
    }
  }
  return result + '"';
}

