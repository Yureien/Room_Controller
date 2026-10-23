#pragma once
#include "Arduino.h"
#include "IRremoteESP8266.h"
#include <array>
#include <cstring>
#include <vector>
constexpr uint8_t kBluestarFanOnly = 1, kBluestarAuto = 2, kBluestarDry = 4,
    kBluestarCool = 8, kBluestarFanTurbo = 0, kBluestarFanHigh = 1,
    kBluestarFanMed = 2, kBluestarFanLow = 4, kBluestarFanAuto = 7,
    kBluestarMinTemp = 16, kBluestarMaxTemp = 30, kBluestarCapacityTurbo = 1,
    kBluestarCapacity80 = 2, kBluestarCapacity60 = 3, kBluestarCapacity40 = 4,
    kBluestarCapacityDefault = 5;
constexpr uint16_t kBluestarCommandPower = 0x5CD, kBluestarCommandTemp = 0x5C4,
    kBluestarCommandTurbo = 0x7C9;
using Packet = std::array<uint8_t, kBluestarStateLength>;
extern std::vector<Packet> packets;
// Models packet fields and Power's target clamp, not IR modulation/reception.
class IRBluestarAc {
 public:
  explicit IRBluestarAc(uint16_t) { raw[0] = 0x33; raw[1] = 0x88; raw[3] = 24; raw[8] = 5; }
  void begin() {}
  void send() { getRaw(); packets.push_back(raw); }
  uint8_t* getRaw() { raw[9] = checksum(raw.data()); return raw.data(); }
  bool setRaw(const uint8_t* data) { std::memcpy(raw.data(), data, raw.size()); return true; }
  static uint8_t checksum(const uint8_t* data) {
    uint8_t sum = 0;
    for (int i = 0; i < 9; ++i) sum += data[i];
    return ~sum;
  }
  static bool validChecksum(const uint8_t* data) { return data[9] == checksum(data); }
  void setCommand(uint16_t value) { raw[10] = value >> 8; raw[11] = value; }
  bool getPower() const { return raw[2] & 0x80; }
  void setPower(bool on) {
    bit(2, 0x80, on);
    if (getTemp() < 24) raw[3] = (raw[3] & 0xE0) | 24;
    setCommand(kBluestarCommandPower);
  }
  uint8_t getMode() const { return raw[1] & 15; }
  void setMode(uint8_t mode) { raw[1] = (raw[1] & 0xF0) | mode; setCommand(0x5C0); }
  uint8_t getTemp() const { return raw[3] & 31; }
  void setTemp(uint8_t value) { raw[3] = (raw[3] & 0xE0) | value; setCommand(kBluestarCommandTemp); }
  uint8_t getFan() const { return raw[1] >> 5; }
  void setFan(uint8_t value) { raw[1] = (raw[1] & 31) | (value << 5); setCommand(0x7D0); }
  uint8_t getCapacity() const { return raw[8] & 7; }
  void setCapacity(uint8_t value) { raw[8] = value; setCommand(0); }
  bool getTurbo() const { return raw[2] & 0x10; }
  void setTurbo(bool on) { bit(2, 0x10, on); setCommand(kBluestarCommandTurbo); }
  bool getEcono() const { return raw[3] & 0x20; }
  void setEcono(bool on) { bit(3, 0x20, on); setCommand(0x7C1); }
  bool getSleep() const { return raw[2] & 0x40; }
  void setSleep(bool on) { bit(2, 0x40, on); setCommand(0x7CC); }
  bool getSwingH() const { return raw[2] & 1; }
  void setSwingH(bool on) { bit(2, 1, on); setCommand(0x5C8); }
  bool getSwingV() const { return raw[2] & 2; }
  void setSwingV(bool on) { bit(2, 2, on); setCommand(0x5C8); }
  bool getLight() const { return !(raw[2] & 8); }
  void setLight(bool on) { bit(2, 8, !on); setCommand(0); }
  String toString() const { return "AC fake"; }
 private:
  Packet raw{};
  void bit(size_t index, uint8_t mask, bool on) {
    if (on) raw[index] |= mask; else raw[index] &= ~mask;
  }
};
