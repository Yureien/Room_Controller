// Room Controller: AC infrared, temperature sensing, and BLE lighting.
// No IR commands are transmitted at boot or when restoring Wi-Fi settings.

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <new>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <driver/uart.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "web_ui.h"
#include "lotus_lamp.h"
#include "json_utils.h"
#include "input_utils.h"
#include "ac_controller.h"
#include "temperature_sensor.h"
#include "temperature_history.h"
#include "temperature_history_api.h"
#include "web_dispatch.h"
#include "web_response.h"
#include "auto_cool.h"
#include "power_config.h"
#include "activity_led.h"

const uint16_t kIrLed = 23;
const uint8_t kTemperaturePin = 22;
const uint8_t kActivityLedPin = 2;  // NodeMCU-32S blue user LED.
const uint8_t kActivityLedOnLevel = HIGH;  // Reference board is active-high.
const uint32_t kTemperatureInterval = TemperatureHistory::kIntervalSeconds * 1000;
const uint32_t kSettingsVersion = 1;
const uint32_t kConnectTimeout = 15000;
const uint32_t kReconnectInterval = 30000;

#if ROOM_CONTROLLER_POWER_SAVING && (!CONFIG_PM_ENABLE || !CONFIG_FREERTOS_USE_TICKLESS_IDLE)
#error "Automatic light sleep requires the Arduino + ESP-IDF build and sdkconfig.defaults"
#endif

struct NetworkSettings {
  uint32_t version;
  char ssid[33];
  char password[65];
  char hostname[64];
};

AcController acController(kIrLed);
TemperatureSensor sensor(kTemperaturePin);
LotusLampController lamp;
AsyncWebServer server(80);
WebDispatch webDispatch;
std::atomic<bool> webActivity{false};
uint64_t webStateRevision = 0;  // Main-loop snapshots; orders concurrent replies.
Preferences storage;
NetworkSettings settings{};
bool storageReady = false;
bool networkUp = false;
bool connecting = false;
uint32_t connectionStarted = 0;
uint32_t listenerAttemptedAt = 0;
bool listenerAttempted = false;
#if ROOM_CONTROLLER_POWER_SAVING
uint32_t lastUserActivity = 0;
bool cpuBoosted = false;
bool powerManagementReady = false;
esp_pm_lock_handle_t cpuLock = nullptr, awakeLock = nullptr;
uint32_t lastSerialActivity = 0;
bool haveSerialActivity = false;
#endif
ActivityLed activityLed;

enum class SetupStep { None, Ssid, Password };
SetupStep setupStep = SetupStep::None;
String pendingSsid;

TemperatureHistory temperatureHistory;
char temperatureHistorySession[9];

void acquireActivePower() {
#if ROOM_CONTROLLER_POWER_SAVING
  if (powerManagementReady && !cpuBoosted) {
    esp_pm_lock_acquire(cpuLock);
    esp_pm_lock_acquire(awakeLock);
    cpuBoosted = true;
  }
#endif
}

void boostCpu() {
#if ROOM_CONTROLLER_POWER_SAVING
  lastUserActivity = millis();
  acquireActivePower();
#endif
}

void beginPowerManagement() {
#if ROOM_CONTROLLER_POWER_SAVING
  // Locks preserve 240 MHz and prevent sleep during IR, BLE, and setup. Keep
  // APB at 80 MHz even at the minimum CPU speed for UART and OneWire timing.
  esp_err_t error = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "controls", &cpuLock);
  if (error == ESP_OK)
    error = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "controls", &awakeLock);
  if (error == ESP_OK) error = uart_set_wakeup_threshold(UART_NUM_0, 3);
  if (error == ESP_OK) error = esp_sleep_enable_uart_wakeup(UART_NUM_0);
  const esp_pm_config_esp32_t config = {240, 80, true};
  if (error == ESP_OK) error = esp_pm_configure(&config);
  if (error != ESP_OK) {
    // SDK startup frequency is 240 MHz. Leave it alone if PM cannot start.
    if (cpuLock) { esp_pm_lock_delete(cpuLock); cpuLock = nullptr; }
    if (awakeLock) { esp_pm_lock_delete(awakeLock); awakeLock = nullptr; }
    Serial.printf("[power] Automatic light sleep unavailable: %s\n", esp_err_to_name(error));
    return;
  }
  powerManagementReady = true;
  boostCpu();
  Serial.println(F("[power] Automatic light sleep; CPU 80..240 MHz; Wi-Fi MIN_MODEM"));
  Serial.println(F("[power] Press Enter before serial commands after idle; serial stays awake for 60s."));
#else
  // Keep the opt-in SDK support compiled, but disable DFS and automatic sleep.
#if CONFIG_PM_ENABLE
  const esp_pm_config_esp32_t config = {240, 240, false};
  const esp_err_t error = esp_pm_configure(&config);
  if (error != ESP_OK)
    Serial.printf("[power] Fixed CPU configuration failed: %s\n", esp_err_to_name(error));
#endif
  Serial.println(F("[power] Savings disabled; CPU fixed at 240 MHz; Wi-Fi sleep off"));
#endif
}

void serviceCpuPower() {
#if ROOM_CONTROLLER_POWER_SAVING
  if (!powerManagementReady) return;
  if (setupStep != SetupStep::None || connecting || lamp.needsFastCpu() ||
      (haveSerialActivity && millis() - lastSerialActivity < 60000)) {
    acquireActivePower();
  } else if (cpuBoosted && millis() - lastUserActivity >= 5000) {
    // The IDF scheduler/drivers now manage DFS and automatic light sleep;
    // never manually change CPU clocks behind their power locks.
    esp_pm_lock_release(awakeLock);
    esp_pm_lock_release(cpuLock);
    cpuBoosted = false;
  }
#endif
}

void serviceActivityLed() {
  const bool autoCoolTurbo = acController.autoCoolTurboActive();
  const bool on = activityLed.on(millis(), lamp.needsFastCpu(), autoCoolTurbo);
  digitalWrite(kActivityLedPin, on ? kActivityLedOnLevel : !kActivityLedOnLevel);
}

void beforeIr() {
  boostCpu();
  activityLed.acCommand(millis());
  digitalWrite(kActivityLedPin, kActivityLedOnLevel);
}
void afterIr() { activityLed.acCommand(millis()); }
void printTemperature() { sensor.printStatus(Serial); }
void printState() {
  acController.printState();
  printTemperature();
}

bool validHostname(const String &name) {
  if (name.length() == 0 || name.length() > 63 || name[0] == '-' ||
      name[name.length() - 1] == '-') return false;
  for (size_t i = 0; i < name.length(); i++) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
      return false;
  }
  return true;
}


bool validPassword(const String &password) {
  if (password.length() == 0) return true;  // Open network.
  if (password.length() >= 8 && password.length() <= 63) return true;
  if (password.length() != 64) return false;
  for (size_t i = 0; i < password.length(); i++)
    if (hexDigit(password[i]) < 0) return false;
  return true;  // A 64-character hexadecimal WPA key.
}

bool validSettings(const NetworkSettings &value) {
  return value.version == kSettingsVersion &&
      std::memchr(value.ssid, '\0', sizeof(value.ssid)) &&
      std::memchr(value.password, '\0', sizeof(value.password)) &&
      std::memchr(value.hostname, '\0', sizeof(value.hostname)) &&
      validHostname(String(value.hostname)) &&
      validPassword(String(value.password));
}

void loadSettings() {
  settings.version = kSettingsVersion;
  snprintf(settings.hostname, sizeof(settings.hostname), "esp32");
  // Keep the legacy NVS namespace so existing settings survive the rename.
  storageReady = storage.begin("ac-controller", false);
  if (!storageReady) {
    Serial.println(F("Flash storage unavailable; Wi-Fi and AC settings cannot save."));
    return;
  }
  NetworkSettings saved{};
  if (storage.getBytesLength("network") == sizeof(saved) &&
      storage.getBytes("network", &saved, sizeof(saved)) == sizeof(saved) &&
      validSettings(saved)) settings = saved;
}

bool saveSettings(const NetworkSettings &value) {
  if (storageReady && value.version == settings.version &&
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
  return true;
}







void stopNetworkServices() {
  webDispatch.setOnline(false);
  // Keep the listener bound across transient link loss. Closing/rebinding port
  // 80 can fail while existing sockets are active or in TCP TIME_WAIT.
  MDNS.end();
  networkUp = false;
  listenerAttempted = false;
}

void connectWifi() {
  boostCpu();
  stopNetworkServices();
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

void printNetworkStatus() {
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

void beginWifiSetup() {
  if (setupStep != SetupStep::None) return;
  pendingSsid = "";
  setupStep = SetupStep::Ssid;
  Serial.println(F("Wi-Fi setup. Type /cancel to leave setup."));
  Serial.println(F("Enter SSID (case-sensitive), then press Enter:"));
}

void handleSetupInput(const String &line) {
  if (line == "/cancel") {
    setupStep = SetupStep::None;
    pendingSsid = "";
    Serial.println(F("Wi-Fi setup cancelled; existing settings retained."));
    return;
  }
  if (setupStep == SetupStep::Ssid) {
    // Ignore empty input without repeatedly printing the setup prompt.
    if (line.length() == 0) return;
    if (line.length() > 32) {
      Serial.println(F("SSID must contain 1..32 bytes. Enter SSID:"));
      return;
    }
    pendingSsid = line;
    setupStep = SetupStep::Password;
    Serial.println(F("Enter Wi-Fi password (case-sensitive), then Enter."));
    Serial.println(F("Firmware does not echo it. Empty = open network:"));
    return;
  }
  if (!validPassword(line)) {
    Serial.println(F("Use 8..63 bytes, a 64-digit hex key, or an empty line."));
    Serial.println(F("Enter password again, or /cancel:"));
    return;
  }
  NetworkSettings updated = settings;
  std::memset(updated.ssid, 0, sizeof(updated.ssid));
  std::memset(updated.password, 0, sizeof(updated.password));
  pendingSsid.toCharArray(updated.ssid, sizeof(updated.ssid));
  line.toCharArray(updated.password, sizeof(updated.password));
  if (!saveSettings(updated)) return;
  pendingSsid = "";
  setupStep = SetupStep::None;
  Serial.println(F("Wi-Fi credentials saved across power loss."));
  connectWifi();
}

void printHelp() {
  Serial.print(F("Room Controller, IR GPIO "));
  Serial.print(kIrLed);
  Serial.print(F(", temperature GPIO "));
  Serial.print(kTemperaturePin);
  Serial.println(F(", 115200 8-N-1. Commands:"));
  Serial.println(F("  on / off / power toggle / temp 16..30|up|down"));
  Serial.println(F("  mode auto|cool|dry|fan|next"));
  Serial.println(F("  fan auto|low|medium|high|next"));
  Serial.println(F("  capacity 100|110|80|60|40|next"));
  Serial.println(F("  horizontal|vertical|light|turbo|econo|sleep"));
  Serial.println(F("    on|off|toggle"));
  Serial.println(F("  state / send / load <24 hexadecimal digits> / help"));
  Serial.println(F("  sensor   (measured temperature on GPIO 22; updates every 5s)"));
  Serial.println(F("  autocool on|off|toggle (AC on in Cool mode; valid sensor required)"));
  Serial.println(F("  wifi setup / wifi status / wifi reconnect"));
  Serial.println(F("  wifi forget"));
  Serial.println(F("  hostname [new-name]   (without .local; saved in flash)"));
  Serial.println(F("  lamp scan / lamp pair <number> / lamp status / lamp forget"));
  Serial.println(F("  lamp connect / lamp reconnect / lamp disconnect / lamp on / lamp off"));
  Serial.println(F("  lamp color <name|#RRGGBB>"));
  Serial.println(F("  lamp brightness 0..100 / lamp effect 0..212 / lamp speed 0..100"));
}




// Serial and web share this handler. Only validated actions send IR.

String stateJson(const String &message = "", const bool ok = true) {
  String result;
  result.reserve(1100);
  result = "{\"ok\":";
  result += ok ? "true" : "false";
  char revision[24];
  snprintf(revision, sizeof(revision), "%llu", static_cast<unsigned long long>(++webStateRevision));
  result += ",\"revision\":";
  result += revision;
  result += ",\"session\":" + jsonString(String(temperatureHistorySession));
  result += ",\"message\":" + jsonString(message);
  result += ",\"hostname\":" + jsonString(String(settings.hostname));
  result += ",\"ip\":" + jsonString(WiFi.localIP().toString());
  acController.appendJson(result);
  sensor.appendJson(result);
  result += ",\"lamp\":" + lamp.json() + "}";
  return result;
}

// Main loop only: command execution and snapshots retain a single owner.
void serviceWebRequests() {
  if (webActivity.exchange(false)) boostCpu();
  WebDispatch::Work work;
  if (!webDispatch.claim(millis(), work)) return;
  const auto& input = work.input;
  String body, message;
  int code = 200;
  if (input.kind == WebDispatch::Kind::History) {
    static char page[TemperatureHistoryApi::kResponseSize];
    const size_t length = TemperatureHistoryApi::formatPage(temperatureHistory,
        esp_timer_get_time() / 1000000, temperatureHistorySession, input.cursor,
        input.reset, input.limit, page);
    webDispatch.complete(work.token, 200, page, length);
    return;
  }
  if (input.kind == WebDispatch::Kind::AcCommand) {
    const bool ok = acController.command(String(input.command), &message, sensor.snapshot());
    code = ok ? 200 : 400;
    body = stateJson(message, ok);
    if (ok) {
      Serial.print(F("Web: "));
      Serial.println(input.command);
      printState();
    }
  } else if (input.kind == WebDispatch::Kind::LampCommand) {
    const bool ok = lamp.command(String(input.command), false, message);
    if (!message.length()) message = "Invalid lamp command.";
    code = ok ? 202 : 400;
    body = stateJson(message, ok);
  } else {
    body = stateJson();
  }
  webDispatch.complete(work.token, code, body.c_str(), body.length());
}

// AsyncTCP task only: inspect request-owned data and hand off bounded copies.
void sendWebError(AsyncWebServerRequest* request, int code, const char* message) {
  String body = "{\"ok\":false,\"message\":";
  body += jsonString(String(message)) + "}";
  auto* response = request->beginResponse(code, "application/json", body);
  if (!response) { request->abort(); return; }
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void enqueueWebRequest(AsyncWebServerRequest* request, const WebDispatch::Input& input) {
  webActivity.store(true);
  const auto token = webDispatch.submit(input, millis());
  if (!token) {
    sendWebError(request, 503, "Controller busy or disconnected. Check state before retrying.");
    return;
  }
  auto* response = new (std::nothrow) ControllerWebResponse(webDispatch, token);
  if (!response) {
    webDispatch.cancel(token);
    sendWebError(request, 503, "Controller busy. Check state before retrying.");
    return;
  }
  request->send(response);
}

void enqueueWebCommand(AsyncWebServerRequest* request, WebDispatch::Kind kind) {
  const String& origin = request->header("Origin");
  if (origin.length() && origin != "http://" + request->host()) {
    sendWebError(request, 403, "Use the controller page.");
    return;
  }
  const auto* parameter = request->getParam("command", true);
  const size_t maximum = kind == WebDispatch::Kind::AcCommand ? 127 : 100;
  if (!parameter || !parameter->value().length() || parameter->value().length() > maximum ||
      parameter->value().length() != strlen(parameter->value().c_str())) {
    sendWebError(request, 400, "Invalid command.");
    return;
  }
  WebDispatch::Input input;
  input.kind = kind;
  memcpy(input.command, parameter->value().c_str(), parameter->value().length() + 1);
  enqueueWebRequest(request, input);
}

void configureWebServer() {
  snprintf(temperatureHistorySession, sizeof(temperatureHistorySession), "%08lx",
           static_cast<unsigned long>(esp_random()));
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    webActivity.store(true);
    // Immutable PROGMEM page; the library streams it without a heap-sized copy.
    auto* response = request->beginResponse(200, "text/html; charset=utf-8",
        reinterpret_cast<const uint8_t*>(kControlPage), sizeof(kControlPage) - 1);
    if (!response) { request->abort(); return; }
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* request) {
    enqueueWebRequest(request, WebDispatch::Input{});
  });
  server.on("/api/temperature/history", HTTP_GET, [](AsyncWebServerRequest* request) {
    WebDispatch::Input input;
    input.kind = WebDispatch::Kind::History;
    uint64_t limit = TemperatureHistoryApi::kPageSize;
    const auto* limitArg = request->getParam("limit");
    const auto* cursorArg = request->getParam("cursor");
    const auto* sessionArg = request->getParam("session");
    if ((limitArg && (!TemperatureHistoryApi::parseUnsigned(limitArg->value().c_str(),
            TemperatureHistoryApi::kPageSize, limit) || !limit)) ||
        (cursorArg && (!sessionArg || !TemperatureHistoryApi::parseUnsigned(
            cursorArg->value().c_str(), TemperatureHistoryApi::kMaxCursor, input.cursor)))) {
      sendWebError(request, 400, "Use limit 1..128 and cursor with session.");
      return;
    }
    input.limit = static_cast<uint16_t>(limit);
    input.reset = !cursorArg || sessionArg->value() != temperatureHistorySession;
    enqueueWebRequest(request, input);
  });
  server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest* request) {
    enqueueWebCommand(request, WebDispatch::Kind::AcCommand);
  });
  server.on("/api/lamp/command", HTTP_POST, [](AsyncWebServerRequest* request) {
    enqueueWebCommand(request, WebDispatch::Kind::LampCommand);
  });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) { request->send(204); });
  server.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });
}

void serviceNetwork() {
  if (!settings.ssid[0]) return;
  if (WiFi.status() == WL_CONNECTED) {
    connecting = false;
    if (!networkUp) {
      networkUp = true;
      if (MDNS.begin(settings.hostname)) MDNS.addService("http", "tcp", 80);
      else Serial.println(F("mDNS unavailable; use the printed IP address."));
      printNetworkStatus();
    }
    if (server.state() != LISTEN &&
        (!listenerAttempted || millis() - listenerAttemptedAt >= 5000)) {
      listenerAttemptedAt = millis();
      listenerAttempted = true;
      server.begin();
      if (server.state() != LISTEN)
        Serial.println(F("HTTP listener unavailable; retrying in 5 seconds."));
    }
    webDispatch.setOnline(server.state() == LISTEN);
    return;
  }
  if (networkUp) {
    stopNetworkServices();
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
    boostCpu();
    WiFi.reconnect();
    connectionStarted = millis();
    connecting = true;
    Serial.println(F("Retrying saved Wi-Fi."));
  }
}

void processSerialCommand(String line) {
  boostCpu();
  if (setupStep != SetupStep::None) {
    handleSetupInput(line);  // Preserve case and spaces in credentials.
    return;
  }
  line.trim();
  if (line.length() == 0) return;
  const int space = line.indexOf(' ');
  String command = space < 0 ? line : line.substring(0, space);
  String value = space < 0 ? "" : line.substring(space + 1);
  command.toLowerCase();
  value.trim();
  if (command == "wifi") {
    value.toLowerCase();
    if (value == "setup") {
      beginWifiSetup();
    } else if (value == "status" || value.length() == 0) {
      printNetworkStatus();
    } else if (value == "reconnect") {
      connectWifi();
    } else if (value == "forget") {
      NetworkSettings updated = settings;
      std::memset(updated.ssid, 0, sizeof(updated.ssid));
      std::memset(updated.password, 0, sizeof(updated.password));
      if (saveSettings(updated)) {
        connectWifi();  // Disconnect and stop serving.
        Serial.println(F("Saved Wi-Fi credentials forgotten."));
        beginWifiSetup();
      }
    } else {
      printHelp();
    }
    return;
  }
  if (command == "hostname") {
    if (!value.length()) {
      printNetworkStatus();
      return;
    }
    value.toLowerCase();
    if (!validHostname(value)) {
      Serial.println(F("Use 1..63 letters/digits/hyphens, without .local."));
      Serial.println(F("The first and last character cannot be a hyphen."));
      return;
    }
    if (value == settings.hostname) {
      printNetworkStatus();
      return;
    }
    NetworkSettings updated = settings;
    value.toCharArray(updated.hostname, sizeof(updated.hostname));
    if (saveSettings(updated)) {
      Serial.println(F("Hostname saved. Reconnecting with the new name."));
      connectWifi();
    }
    return;
  }
  if (command == "help" && !value.length()) {
    printHelp();
    return;
  }
  if (command == "sensor" && !value.length()) {
    printTemperature();
    return;
  }
  if (command == "lamp") {
    value.toLowerCase();
    String message;
    lamp.command(value, true, message);
    Serial.println(message);
    if (!value.length() || value == "status") lamp.printStatus();
    return;
  }
  String message;
  if (acController.command(line, &message, sensor.snapshot())) {
    Serial.println(message);
    printState();
  } else {
    Serial.println(message);
  }
}

void readSerial() {
  static char line[192];
  static size_t length = 0;
  static bool overflow = false;
  static bool previousCr = false;
#if ROOM_CONTROLLER_POWER_SAVING
  static bool uartWakeHandled = false;
  const bool uartWake = powerManagementReady &&
      esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UART;
  if ((uartWake && !uartWakeHandled) || Serial.available()) {
    const bool serialWasIdle = !haveSerialActivity || millis() - lastSerialActivity >= 60000;
    lastSerialActivity = millis();
    haveSerialActivity = true;
    boostCpu();
    if (uartWake && !uartWakeHandled) {
      // UART wake-up loses characters. Discard the wake input so a truncated
      // "lamp off", for example, cannot be interpreted as an AC "off" command.
      // The serial monitor sends a complete line at 115200; let it finish first.
      delay(20);
      uart_flush_input(UART_NUM_0);
      std::memset(line, 0, sizeof(line));
      length = 0;
      overflow = previousCr = false;
    }
    if (powerManagementReady && setupStep == SetupStep::None &&
        (serialWasIdle || (uartWake && !uartWakeHandled)))
      Serial.println(F("Serial awake. Enter your command."));
  }
  uartWakeHandled = uartWake;
#endif
  // A noisy or continuously active serial port must not starve the main loop.
  size_t processed = 0;
  while (Serial.available() && processed++ < 64) {
    const uint8_t c = Serial.read();
    if (c == '\n' && previousCr) {
      previousCr = false;  // CRLF is one line, including password prompts.
      continue;
    }
    previousCr = c == '\r';
    if (c == '\r' || c == '\n') {
      if (overflow) {
        Serial.println(F("Command too long; discarded."));
      } else if (length || setupStep != SetupStep::None) {
        line[length] = '\0';
        processSerialCommand(String(line));
      }
      std::memset(line, 0, sizeof(line));
      length = 0;
      overflow = false;
      // An input burst can contain many short IR commands. Limit execution,
      // not just bytes, so sensors and HTTP get a turn between commands.
      break;
    } else if (!overflow && (c == '\b' || c == 127)) {
      if (length) length--;
    } else if (!overflow && c >= ' ') {
      if (length + 1 < sizeof(line)) line[length++] = c;
      else overflow = true;
    }
  }
}

const char *resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on/reset pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "exception/panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "other";
  }
}

void setup() {
  // Boot is active work at the SDK's 240 MHz startup frequency. Initialize the
  // output level before enabling it to avoid an unwanted LED glitch.
  digitalWrite(kActivityLedPin, kActivityLedOnLevel);
  pinMode(kActivityLedPin, OUTPUT);
  Serial.begin(115200, SERIAL_8N1);
  // Let the USB/serial monitor settle before printing startup text.
  delay(300);
  const esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("\n[boot] reset=%s (%d)\n", resetReasonName(reason),
                static_cast<int>(reason));
  beginPowerManagement();
  acController.begin(beforeIr, afterIr);
  sensor.begin();
  loadSettings();
  lamp.begin();
  configureWebServer();
  printHelp();
  printState();
  Serial.flush();
  if (settings.ssid[0]) connectWifi();
  else beginWifiSetup();
}

void loop() {
  readSerial();
  lamp.service(setupStep == SetupStep::None);
  if (sensor.service() && setupStep == SetupStep::None) printTemperature();
  acController.service(sensor.snapshot(), setupStep == SetupStep::None);
  const auto reading = sensor.snapshot();
  if (reading.status != TemperatureReading::Status::Waiting) {
    temperatureHistory.record(esp_timer_get_time() / 1000000,
                              reading.available(), reading.celsius);
  }
  serviceNetwork();
  serviceWebRequests();
  serviceCpuPower();
  serviceActivityLed();
  delay(20);
}
