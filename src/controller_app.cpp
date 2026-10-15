#include "controller_app.h"
#include "board_config.h"
#include "power_runtime.h"
#include "serial_console.h"
#include "controller_web.h"
#include "temperature_history_api.h"
#include "activity_led.h"
#include "json_utils.h"
#include <esp_system.h>
#include <esp_timer.h>

namespace {
AcController acController(kIrLed);
TemperatureSensor sensor(kTemperaturePin);
LotusLampController lamp;
ControllerNetwork network;
ControllerWeb web;
SerialConsole console(acController, sensor, lamp, network);
TemperatureHistory temperatureHistory;
ActivityLed activityLed;
uint64_t webStateRevision = 0;

void serviceActivityLed() {
  const bool autoCoolTurbo = acController.autoCoolTurboActive();
  const bool on = activityLed.on(millis(), lamp.needsFastCpu(), autoCoolTurbo);
  digitalWrite(kActivityLedPin, on ? kActivityLedOnLevel : !kActivityLedOnLevel);
}

void beforeIr() {
  PowerRuntime::activity();
  activityLed.acCommand(millis());
  digitalWrite(kActivityLedPin, kActivityLedOnLevel);
}
void afterIr() { activityLed.acCommand(millis()); }

String stateJson(const String &message = "", const bool ok = true) {
  String result;
  result.reserve(1100);
  result = "{\"ok\":";
  result += ok ? "true" : "false";
  char revision[24];
  snprintf(revision, sizeof(revision), "%llu", static_cast<unsigned long long>(++webStateRevision));
  result += ",\"revision\":";
  result += revision;
  result += ",\"session\":" + jsonString(String(web.session()));
  result += ",\"message\":" + jsonString(message);
  result += ",\"hostname\":" + jsonString(String(network.hostname()));
  result += ",\"ip\":" + jsonString(network.ip());
  acController.appendJson(result);
  sensor.appendJson(result);
  result += ",\"lamp\":" + lamp.json() + "}";
  return result;
}

// Main loop only: command execution and snapshots retain a single owner.
void serviceWebRequests() {
  if (web.takeActivity()) PowerRuntime::activity();
  WebDispatch::Work work;
  if (!web.next(work)) return;
  const auto& input = work.input;
  String body, message;
  int code = 200;
  if (input.kind == WebDispatch::Kind::History) {
    static char page[TemperatureHistoryApi::kResponseSize];
    const size_t length = TemperatureHistoryApi::formatPage(temperatureHistory,
        esp_timer_get_time() / 1000000, web.session(), input.cursor,
        input.reset, input.limit, page);
    web.reply(work.token, 200, page, length);
    return;
  }
  if (input.kind == WebDispatch::Kind::AcCommand) {
    const bool ok = acController.command(String(input.command), &message, sensor.snapshot());
    code = ok ? 200 : 400;
    body = stateJson(message, ok);
    if (ok) {
      Serial.print(F("Web: "));
      Serial.println(input.command);
      console.printState();
    }
  } else if (input.kind == WebDispatch::Kind::LampCommand) {
    const bool ok = lamp.command(String(input.command), false, message);
    if (!message.length()) message = "Invalid lamp command.";
    code = ok ? 202 : 400;
    body = stateJson(message, ok);
  } else {
    body = stateJson();
  }
  web.reply(work.token, code, body.c_str(), body.length());
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

}

void ControllerApp::begin() {
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
  PowerRuntime::begin();
  acController.begin(beforeIr, afterIr);
  sensor.begin();
  network.begin(PowerRuntime::activity);
  lamp.begin();
  web.begin();
  console.printHelp();
  console.printState();
  Serial.flush();
  if (network.configured()) network.connect();
  else console.beginWifiSetup();
}

void ControllerApp::service() {
  console.read();
  lamp.service(!console.setupActive());
  if (sensor.service() && !console.setupActive()) sensor.printStatus(Serial);
  acController.service(sensor.snapshot(), !console.setupActive());
  const auto reading = sensor.snapshot();
  if (reading.status != TemperatureReading::Status::Waiting) {
    temperatureHistory.record(esp_timer_get_time() / 1000000,
                              reading.available(), reading.celsius);
  }
  network.service();
  web.serviceConnection(network.connected());
  serviceWebRequests();
  PowerRuntime::service(console.setupActive() || network.isConnecting() || lamp.needsFastCpu());
  serviceActivityLed();
  delay(20);
}
