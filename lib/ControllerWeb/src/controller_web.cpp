#include "controller_web.h"
#include "web_response.h"
#include "temperature_history_api.h"
#include "json_utils.h"
#include "web_ui.h"
#include <esp_system.h>
#include <new>
#include <cstring>

void ControllerWeb::sendWebError(AsyncWebServerRequest* request, int code, const char* message) {
  String body = "{\"ok\":false,\"message\":";
  body += jsonString(String(message)) + "}";
  auto* response = request->beginResponse(code, "application/json", body);
  if (!response) { request->abort(); return; }
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void ControllerWeb::enqueueWebRequest(AsyncWebServerRequest* request, const WebDispatch::Input& input) {
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

void ControllerWeb::enqueueWebCommand(AsyncWebServerRequest* request, WebDispatch::Kind kind) {
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

void ControllerWeb::begin() {
  snprintf(session_, sizeof(session_), "%08lx",
           static_cast<unsigned long>(esp_random()));
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
    webActivity.store(true);
    // Immutable PROGMEM page; the library streams it without a heap-sized copy.
    auto* response = request->beginResponse(200, "text/html; charset=utf-8",
        reinterpret_cast<const uint8_t*>(kControlPage), sizeof(kControlPage) - 1);
    if (!response) { request->abort(); return; }
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });
  server.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* request) {
    enqueueWebRequest(request, WebDispatch::Input{});
  });
  server.on("/api/temperature/history", HTTP_GET, [this](AsyncWebServerRequest* request) {
    WebDispatch::Input input;
    input.kind = WebDispatch::Kind::History;
    uint64_t limit = TemperatureHistoryApi::kPageSize;
    const auto* limitArg = request->getParam("limit");
    const auto* cursorArg = request->getParam("cursor");
    const auto* sessionArg = request->getParam("session");
    const auto invalidText = [](const AsyncWebParameter* arg) {
      return arg && arg->value().length() != strlen(arg->value().c_str());
    };
    if (invalidText(limitArg) || invalidText(cursorArg) || invalidText(sessionArg) ||
        (limitArg && (!TemperatureHistoryApi::parseUnsigned(limitArg->value().c_str(),
            TemperatureHistoryApi::kPageSize, limit) || !limit)) ||
        (cursorArg && (!sessionArg || !TemperatureHistoryApi::parseUnsigned(
            cursorArg->value().c_str(), TemperatureHistoryApi::kMaxCursor, input.cursor)))) {
      sendWebError(request, 400, "Use limit 1..128 and cursor with session.");
      return;
    }
    input.limit = static_cast<uint16_t>(limit);
    input.reset = !cursorArg || sessionArg->value() != session_;
    enqueueWebRequest(request, input);
  });
  server.on("/api/command", HTTP_POST, [this](AsyncWebServerRequest* request) {
    enqueueWebCommand(request, WebDispatch::Kind::AcCommand);
  });
  server.on("/api/lamp/command", HTTP_POST, [this](AsyncWebServerRequest* request) {
    enqueueWebCommand(request, WebDispatch::Kind::LampCommand);
  });
  server.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest* request) { request->send(204); });
  server.onNotFound([this](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });
}

void ControllerWeb::serviceConnection(bool online) {
  if (!online) {
    webDispatch.setOnline(false);
    listenerAttempted = false;
    return;
  }
  // Leave the listener bound across link loss; failed starts retry independently.
  if (server.state() != LISTEN &&
      (!listenerAttempted || millis() - listenerAttemptedAt >= 5000)) {
    listenerAttempted = true;
    listenerAttemptedAt = millis();
    server.begin();
    if (server.state() != LISTEN && !listenerFailed) {
      Serial.println(F("HTTP listener unavailable; retrying every 5 seconds."));
      listenerFailed = true;
    }
  }
  const bool listening = server.state() == LISTEN;
  if (listening && listenerFailed) {
    Serial.println(F("HTTP listener available."));
    listenerFailed = false;
  }
  webDispatch.setOnline(listening);
}
