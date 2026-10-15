// Lotus Lamp command encoding, ported from wporter82/lotus-lamp-python.
// Copyright (c) 2026 Lotus Lamp Python Contributors. MIT; see THIRD_PARTY_NOTICES.md.
#ifndef LOTUS_PROTOCOL_H_
#define LOTUS_PROTOCOL_H_

#include <cstdint>
#include <cstring>

namespace lotus {
enum class Action : uint8_t { Power, Color, Brightness, Effect, Speed };
struct Command {
  Action action = Action::Power;
  uint8_t values[3]{};
};

inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool number(const char *text, unsigned maximum, uint8_t &value) {
  if (!*text) return false;
  unsigned n = 0;
  for (; *text; text++) {
    if (*text < '0' || *text > '9') return false;
    n = n * 10 + (*text - '0');
    if (n > maximum) return false;
  }
  value = n;
  return true;
}

inline bool color(const char *text, uint8_t rgb[3]) {
  struct Color { const char *name; uint8_t rgb[3]; };
  static const Color colors[] = {
    {"red", {255, 0, 0}}, {"green", {0, 255, 0}}, {"blue", {0, 0, 255}},
    {"white", {255, 255, 255}}, {"yellow", {255, 255, 0}},
    {"cyan", {0, 255, 255}}, {"magenta", {255, 0, 255}},
    {"orange", {255, 165, 0}}, {"purple", {128, 0, 128}},
    {"pink", {255, 192, 203}}, {"black", {0, 0, 0}}
  };
  for (const auto &preset : colors) {
    if (std::strcmp(text, preset.name) == 0) {
      std::memcpy(rgb, preset.rgb, 3);
      return true;
    }
  }
  if (*text == '#') text++;
  if (std::strlen(text) != 6) return false;
  for (unsigned i = 0; i < 3; i++) {
    int high = hexDigit(text[2 * i]), low = hexDigit(text[2 * i + 1]);
    if (high < 0 || low < 0) return false;
    rgb[i] = (high << 4) | low;
  }
  return true;
}

inline bool parse(const char *text, Command &result) {
  if (std::strcmp(text, "on") == 0 || std::strcmp(text, "off") == 0) {
    result.action = Action::Power;
    result.values[0] = text[1] == 'n';
    return true;
  }
  if (std::strncmp(text, "color ", 6) == 0) {
    result.action = Action::Color;
    return color(text + 6, result.values);
  }
  struct Numeric { const char *prefix; unsigned length, maximum; Action action; };
  static const Numeric commands[] = {
    {"brightness ", 11, 100, Action::Brightness},
    {"effect ", 7, 212, Action::Effect}, {"speed ", 6, 100, Action::Speed}
  };
  for (const auto &command : commands) {
    if (std::strncmp(text, command.prefix, command.length) == 0) {
      result.action = command.action;
      return number(text + command.length, command.maximum, result.values[0]);
    }
  }
  return false;
}

inline void encode(const Command &command, uint8_t packet[9]) {
  const uint8_t base[] = {0x7E, 0x07, 0, command.values[0], 0xFF, 0xFF, 0xFF, 0, 0xEF};
  std::memcpy(packet, base, sizeof(base));
  switch (command.action) {
    case Action::Power: packet[2] = 4; packet[4] = 0; break;
    case Action::Color:
      packet[2] = 5; packet[3] = 3;
      std::memcpy(packet + 4, command.values, 3);
      packet[7] = 0x10;
      break;
    case Action::Brightness: packet[2] = 1; break;
    case Action::Effect: packet[2] = 3; break;
    case Action::Speed: packet[1] = 4; packet[2] = 2; break;
  }
}
}  // namespace lotus
#endif
