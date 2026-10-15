#pragma once
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "temperature_reading.h"

// Main-loop only. Discovery examines at most one address per service call.
class TemperatureSensor {
 public:
  explicit TemperatureSensor(uint8_t pin) : pin_(pin), bus_(pin), sensor_(&bus_) {}
  TemperatureSensor(const TemperatureSensor&) = delete;
  TemperatureSensor& operator=(const TemperatureSensor&) = delete;
  void begin();
  bool service();
  TemperatureReading snapshot() const;
  void printStatus(Print& output) const;
  void appendJson(String& result) const;
 private:
  enum class Phase { Discover, Configure, Convert, Retry };
  const uint8_t pin_;
  OneWire bus_;
  DallasTemperature sensor_;
  DeviceAddress address_{};
  TemperatureReading reading_;
  Phase phase_ = Phase::Discover;
  uint32_t started_ = 0;
  int64_t conversionReadyUs_ = 0;
  uint8_t searched_ = 0;
  bool parasite_ = false;
  bool startConversion();
  bool unavailable();
};
