#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <IRremoteESP8266.h>
#include <ir_Bluestar.h>
#include "auto_cool.h"
#include "temperature_reading.h"

// Single main-loop owner. Hooks bracket every physical packet, including recovery.
class AcController {
 public:
  explicit AcController(uint16_t pin) : ac(pin), irPin(pin) {}
  AcController(const AcController&) = delete;
  AcController& operator=(const AcController&) = delete;
  void begin(void (*before)(), void (*after)());
  bool command(String line, String* message, const TemperatureReading& reading);
  void service(const TemperatureReading& reading, bool printEvents = true);
  void appendJson(String& result);
  void printState();
  bool autoCoolTurboActive() const { return autoCool.enabled() && ac.getPower() && ac.getTurbo(); }
  uint32_t packetsSent() const { return commandsSent; }
 private:
  struct SavedAcState {
    uint32_t version;
    uint8_t raw[kBluestarStateLength];
    uint8_t normalFan;  // Fan to restore when returning to 100% capacity.
  };

  struct SavedAutoCool {
    uint32_t version;
    uint8_t target;
    uint8_t enabled;
    uint16_t reserved;
  };
  static_assert(sizeof(SavedAutoCool) == 8, "Unexpected Auto-Cool settings size");

  static_assert(sizeof(SavedAcState) == 20, "Legacy AC NVS layout changed");
  IRBluestarAc ac;
  const uint16_t irPin;
  AutoCool autoCool;
  Preferences storage;
  bool storageReady = false;
  SavedAcState lastSavedAcState{};
  SavedAutoCool lastSavedAutoCool{};
  bool haveSavedAcState = false, haveSavedAutoCool = false;
  bool autoCoolSettingsSaved = true, acStateSaved = true;
  uint8_t normalFan = kBluestarFanLow;
  uint32_t commandsSent = 0;
  void (*beforeIr)() = nullptr;
  void (*afterIr)() = nullptr;
  void sendAcPacket();
  void restoreAutoCoolTarget();
  void stopAutoCool();
  void loadAcState();
  void loadAutoCoolSettings();
  bool saveAcState();
  bool saveAutoCoolSettings();
  bool loadState(String hex);
};
