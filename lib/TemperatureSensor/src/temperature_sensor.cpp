#include "temperature_sensor.h"
#include "json_utils.h"
#include <esp_timer.h>
#include <cmath>

namespace {
constexpr uint32_t kIntervalMs = 5000;
constexpr uint32_t kConversionMs = 750;
constexpr uint8_t kMaxAddresses = 32;
uint32_t monotonicSeconds() { return esp_timer_get_time() / 1000000; }
}

void TemperatureSensor::begin() {
  sensor_.setWaitForConversion(false);
  sensor_.setAutoSaveScratchPad(false);
  bus_.reset_search();
  phase_ = Phase::Discover;
  searched_ = 0;
}

bool TemperatureSensor::unavailable() {
  const bool changed = reading_.status != TemperatureReading::Status::Unavailable;
  reading_.status = TemperatureReading::Status::Unavailable;
  phase_ = Phase::Retry;
  started_ = millis();
  return changed;
}

bool TemperatureSensor::startConversion() {
  if (!bus_.reset()) return false;
  bus_.select(address_);
  // Dallas begin() is deliberately bypassed to avoid its repeated discovery
  // delays. Preserve its parasite-power handling explicitly for this address.
  bus_.write(0x44, parasite_);
  started_ = millis();
  conversionSampleSeconds_ = (esp_timer_get_time() + 750000) / 1000000;
  phase_ = Phase::Convert;
  return true;
}

bool TemperatureSensor::service() {
  const uint32_t elapsed = millis() - started_;
  switch (phase_) {
    case Phase::Discover:
      if (searched_++ >= kMaxAddresses || !bus_.search(address_)) return unavailable();
      if (address_[0] == 0x28 && OneWire::crc8(address_, 7) == address_[7])
        phase_ = Phase::Configure;
      return false;
    case Phase::Configure:
      // These Dallas operations are bounded single-address transactions. No
      // scratchpad EEPROM copy is requested, even if resolution must change.
      if (!sensor_.setResolution(address_, 12, true)) {
        phase_ = Phase::Discover;
        return false;
      }
      parasite_ = sensor_.readPowerSupply(address_);
      if (!startConversion()) return unavailable();
      return false;
    case Phase::Convert: {
      if (elapsed < kConversionMs) return false;
      // Do not reset/release the strong data-line pull-up during conversion.
      bus_.depower();
      // Dallas validates scratchpad CRC and rejects disconnected/all-zero data.
      const float value = sensor_.getTempC(address_);
      if (!std::isfinite(value) || value < -55 || value > 125) return unavailable();
      reading_.status = TemperatureReading::Status::Ready;
      reading_.celsius = value;
      reading_.sampledAtSeconds = conversionSampleSeconds_;
      phase_ = Phase::Retry;
      // Keep cadence measured from conversion start, without catch-up bursts.
      return true;
    }
    case Phase::Retry:
      if (elapsed < kIntervalMs) return false;
      if (reading_.status == TemperatureReading::Status::Ready) {
        if (!startConversion()) return unavailable();
      } else {
        bus_.reset_search();
        searched_ = 0;
        phase_ = Phase::Discover;
      }
      return false;
  }
  return false;
}

TemperatureReading TemperatureSensor::snapshot() const {
  auto result = reading_;
  if (result.available() && monotonicSeconds() - result.sampledAtSeconds > 7)
    result.status = TemperatureReading::Status::Unavailable;
  return result;
}

void TemperatureSensor::printStatus(Print& output) const {
  const auto reading = snapshot();
  output.print(F("Measured temperature (GPIO "));
  output.print(pin_);
  output.print(F("): "));
  if (reading.available()) {
    output.print(reading.celsius, 2);
    output.println(F(" C"));
  } else {
    output.println(reading.status == TemperatureReading::Status::Waiting ?
        F("waiting for first reading") : F("sensor unavailable"));
  }
}

void TemperatureSensor::appendJson(String& result) const {
  const auto reading = snapshot();
  result += ",\"measuredTemperature\":";
  result += reading.available() ? String(reading.celsius, 2) : String("null");
  result += ",\"sensorStatus\":" + jsonString(String(reading.statusName()));
  result += ",\"sensorIntervalSeconds\":5";
}
