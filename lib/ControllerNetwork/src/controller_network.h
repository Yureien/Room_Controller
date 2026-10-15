#pragma once
#include <Arduino.h>
#include <cstddef>
#include <Preferences.h>

class ControllerNetwork {
 public:
  ControllerNetwork() = default;
  ControllerNetwork(const ControllerNetwork&) = delete;
  ControllerNetwork& operator=(const ControllerNetwork&) = delete;
  void begin(void (*activity)());
  void connect();
  void service();
  bool connected() const;
  bool isConnecting() const { return connecting; }
  bool configured() const { return settings.ssid[0]; }
  const char* hostname() const { return settings.hostname; }
  String ip() const;
  void printStatus();
  bool setCredentials(const String& ssid, const String& password);
  bool setHostname(const String& hostname);
  bool forget();
  static bool validHostname(const String& name);
  static bool validPassword(const String& password);
 private:
  struct NetworkSettings {
    uint32_t version;
    char ssid[33];
    char password[65];
    char hostname[64];
  };

  static_assert(sizeof(NetworkSettings) == 168, "Legacy network NVS layout changed");
  static_assert(offsetof(NetworkSettings, ssid) == 4 &&
                offsetof(NetworkSettings, password) == 37 &&
                offsetof(NetworkSettings, hostname) == 102, "Legacy network NVS offsets changed");
  NetworkSettings settings{};
  Preferences storage;
  bool storageReady = false, haveSaved = false;
  bool networkUp = false, connecting = false;
  bool mdnsUp = false, mdnsAttempted = false, mdnsFailed = false;
  uint32_t connectionStarted = 0, mdnsAttemptedAt = 0;
  void (*activity)() = nullptr;
  bool validSettings(const NetworkSettings& value);
  bool saveSettings(const NetworkSettings& value);
  void stopServices();
};
