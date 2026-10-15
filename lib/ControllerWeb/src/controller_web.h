#pragma once
#include <ESPAsyncWebServer.h>
#include <atomic>
#include "web_dispatch.h"

// Application lifetime; begin() registers callbacks once, before networking.
class ControllerWeb {
 public:
  void begin();
  void serviceConnection(bool online);
  bool takeActivity() { return webActivity.exchange(false); }
  bool next(WebDispatch::Work& work) { return webDispatch.claim(millis(), work); }
  void reply(WebDispatch::Token token, int code, const char* body, size_t length) {
    webDispatch.complete(token, code, body, length);
  }
  const char (&session() const)[9] { return session_; }
 private:
  AsyncWebServer server{80};
  WebDispatch webDispatch;
  std::atomic<bool> webActivity{false};
  char session_[9]{};
  bool listenerAttempted = false, listenerFailed = false;
  uint32_t listenerAttemptedAt = 0;
  void sendWebError(AsyncWebServerRequest* request, int code, const char* message);
  void enqueueWebRequest(AsyncWebServerRequest* request, const WebDispatch::Input& input);
  void enqueueWebCommand(AsyncWebServerRequest* request, WebDispatch::Kind kind);
};
