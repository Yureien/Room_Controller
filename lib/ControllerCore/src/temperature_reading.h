#pragma once
#include <cstdint>

// Freshness is resolved when the hardware owner creates this value snapshot.
struct TemperatureReading {
  enum class Status { Waiting, Ready, Unavailable };
  Status status = Status::Waiting;
  float celsius = 0;
  uint32_t sampledAtSeconds = 0;
  bool available() const { return status == Status::Ready; }
  const char* statusName() const {
    return available() ? "ready" : status == Status::Waiting ? "waiting" : "unavailable";
  }
};
