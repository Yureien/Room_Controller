#include "controller_network.h"
#include "input_utils.h"
#include "power_config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <cstring>

namespace {
constexpr uint32_t kSettingsVersion = 1;
constexpr uint32_t kConnectTimeout = 15000;
constexpr uint32_t kReconnectInterval = 30000;
}

bool ControllerNetwork::validHostname(const String &name) {
  if (name.length() == 0 || name.length() > 63 || name[0] == '-' ||
      name[name.length() - 1] == '-') return false;
  for (size_t i = 0; i < name.length(); i++) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
      return false;
  }
  return true;
}

bool ControllerNetwork::validPassword(const String &password) {
  if (password.length() != strlen(password.c_str())) return false;
  if (password.length() == 0) return true;  // Open network.
  if (password.length() >= 8 && password.length() <= 63) return true;
  if (password.length() != 64) return false;
  for (size_t i = 0; i < password.length(); i++)
    if (hexDigit(password[i]) < 0) return false;
  return true;  // A 64-character hexadecimal WPA key.
}

bool ControllerNetwork::validSettings(const NetworkSettings &value) {
  return value.version == kSettingsVersion &&
      std::memchr(value.ssid, '\0', sizeof(value.ssid)) &&
      std::memchr(value.password, '\0', sizeof(value.password)) &&
      std::memchr(value.hostname, '\0', sizeof(value.hostname)) &&
      validHostname(String(value.hostname)) &&
      validPassword(String(value.password));
}

void ControllerNetwork::begin(void (*onActivity)()) {
  activity = onActivity;
  settings.version = kSettingsVersion;
  snprintf(settings.hostname, sizeof(settings.hostname), "esp32");
  // Keep the legacy NVS namespace so existing settings survive the rename.
  storageReady = storage.begin("ac-controller", false);
  if (!storageReady) {
    Serial.println(F("Flash storage unavailable; Wi-Fi settings cannot save."));
    return;
  }
  NetworkSettings saved{};
  if (storage.getBytesLength("network") == sizeof(saved) &&
      storage.getBytes("network", &saved, sizeof(saved)) == sizeof(saved) &&
      validSettings(saved)) {
    settings = saved;
    haveSaved = true;
  }
}

bool ControllerNetwork::saveSettings(const NetworkSettings &value) {
  if (haveSaved && value.version == settings.version &&
      std::strcmp(value.ssid, settings.ssid) == 0 &&
      std::strcmp(value.password, settings.password) == 0 &&
      std::strcmp(value.hostname, settings.hostname) == 0) return true;
  // A single NVS entry keeps the SSID/password pair together across updates.
  if (!storageReady ||
      storage.putBytes("network", &value, sizeof(value)) != sizeof(value)) {
    Serial.println(F("Could not save settings; previous settings retained."));
    return false;
  }
  settings = value;
  haveSaved = true;
  return true;
}

void ControllerNetwork::stopServices() {
  MDNS.end();
  networkUp = false;
  mdnsUp = mdnsAttempted = mdnsFailed = false;
}

void ControllerNetwork::connect() {
  if (activity) activity();
  stopServices();
  WiFi.disconnect(true, false);
  connecting = false;
  if (!settings.ssid[0]) return;
  WiFi.persistent(false);  // Credentials are managed in our NVS entry.
  // Arduino reapplies this preference from its asynchronous STA_START event.
  // Setting only the IDF driver below could be overwritten after begin().
  WiFi.setSleep(ROOM_CONTROLLER_POWER_SAVING ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
  WiFi.setHostname(settings.hostname);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(settings.ssid, settings.password);
  // Optional modem sleep preserves DTIM/mDNS; AC-powered builds stay awake.
  const esp_err_t error = esp_wifi_set_ps(
      ROOM_CONTROLLER_POWER_SAVING ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
  if (error != ESP_OK)
    Serial.printf("Wi-Fi sleep configuration failed: %s\n", esp_err_to_name(error));
  connectionStarted = millis();
  connecting = true;
  Serial.print(F("Connecting to Wi-Fi: "));
  Serial.println(settings.ssid);
}

void ControllerNetwork::printStatus() {
  Serial.print(F("Hostname: "));
  Serial.print(settings.hostname);
  Serial.println(F(".local"));
  if (!settings.ssid[0]) {
    Serial.println(F("No saved Wi-Fi. Use 'wifi setup'."));
  } else {
    Serial.print(F("SSID: "));
    Serial.println(settings.ssid);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(F("Web remote: http://"));
      Serial.print(settings.hostname);
      Serial.println(F(".local/"));
      Serial.print(F("IP address: http://"));
      Serial.print(WiFi.localIP());
      Serial.println('/');
    } else {
      Serial.println(F("Wi-Fi disconnected; saved credentials will retry."));
    }
  }
}

void ControllerNetwork::service() {
  if (!settings.ssid[0]) return;
  if (WiFi.status() == WL_CONNECTED) {
    connecting = false;
    if (!networkUp) {
      networkUp = true;
      printStatus();
    }
    if (!mdnsUp && (!mdnsAttempted || millis() - mdnsAttemptedAt >= 30000)) {
      mdnsAttempted = true;
      mdnsAttemptedAt = millis();
      mdnsUp = MDNS.begin(settings.hostname);
      if (mdnsUp) {
        MDNS.addService("http", "tcp", 80);
        if (mdnsFailed) Serial.println(F("mDNS available."));
      } else if (!mdnsFailed) {
        Serial.println(F("mDNS unavailable; use the printed IP address. Retrying every 30s."));
        mdnsFailed = true;
      }
    }
    return;
  }
  if (networkUp) {
    stopServices();
    Serial.println(F("Wi-Fi lost. Reconnecting; serial controls still work."));
    connectionStarted = millis();
    connecting = true;
  }
  const uint32_t elapsed = millis() - connectionStarted;
  if (connecting && elapsed >= kConnectTimeout) {
    connecting = false;
    Serial.println(F("Wi-Fi not connected. Try wifi setup."));
  }
  if (elapsed >= kReconnectInterval) {
    if (activity) activity();
    WiFi.reconnect();
    connectionStarted = millis();
    connecting = true;
    Serial.println(F("Retrying saved Wi-Fi."));
  }
}

bool ControllerNetwork::connected() const {
  return configured() && WiFi.status() == WL_CONNECTED;
}
String ControllerNetwork::ip() const { return WiFi.localIP().toString(); }

bool ControllerNetwork::setCredentials(const String& ssid, const String& password) {
  if (!ssid.length() || ssid.length() > 32 || ssid.length() != strlen(ssid.c_str()) ||
      !validPassword(password)) return false;
  NetworkSettings updated = settings;
  memset(updated.ssid, 0, sizeof(updated.ssid));
  memset(updated.password, 0, sizeof(updated.password));
  ssid.toCharArray(updated.ssid, sizeof(updated.ssid));
  password.toCharArray(updated.password, sizeof(updated.password));
  return saveSettings(updated);
}
bool ControllerNetwork::setHostname(const String& name) {
  if (!validHostname(name)) return false;
  NetworkSettings updated = settings;
  memset(updated.hostname, 0, sizeof(updated.hostname));
  name.toCharArray(updated.hostname, sizeof(updated.hostname));
  return saveSettings(updated);
}
bool ControllerNetwork::forget() {
  NetworkSettings updated = settings;
  memset(updated.ssid, 0, sizeof(updated.ssid));
  memset(updated.password, 0, sizeof(updated.password));
  return saveSettings(updated);
}
