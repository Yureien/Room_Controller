#ifndef WEB_RESPONSE_H_
#define WEB_RESPONSE_H_

#include <ESPAsyncWebServer.h>
#include <memory>
#include "web_dispatch.h"

// All methods, including destruction on disconnect, run on the AsyncTCP task.
// Unlike calling request->send() from loop(), this never accesses a request
// concurrently with its poll/ACK/disconnect callbacks. No raw pointer is saved.
class ControllerWebResponse : public AsyncWebServerResponse {
 public:
  ControllerWebResponse(WebDispatch& dispatch, WebDispatch::Token token)
      : dispatch_(dispatch), token_(token) {}
  ~ControllerWebResponse() override { dispatch_.cancel(token_); }
  bool _sourceValid() const override { return true; }
  bool _started() const override { return response_ && response_->_started(); }
  bool _finished() const override { return response_ && response_->_finished(); }
  bool _failed() const override { return response_ && response_->_failed(); }
  void _respond(AsyncWebServerRequest* request) override {
    // The server disables its request timeout before calling _respond(). Keep
    // a finite lifetime while waiting for loop() or a client's TCP window. A
    // lost client must not retain one of the bounded dispatch slots forever.
    request->client()->setRxTimeout(10);
    start(request);
  }
  size_t _ack(AsyncWebServerRequest* request, size_t length, uint32_t time) override {
    if (!response_) { start(request); return 0; }
    return response_->_ack(request, length, time);
  }

 private:
  WebDispatch& dispatch_;
  WebDispatch::Token token_;
  std::unique_ptr<AsyncWebServerResponse> response_;
  void start(AsyncWebServerRequest* request) {
    if (response_) return;
    const bool ready = dispatch_.consume(token_, millis(), [&](int code, const char* body) {
      response_.reset(request->beginResponse(code, "application/json", body));
    });
    if (!ready) return;  // AsyncTCP polls again; no blocking or busy wait.
    // request->abort() stops working after the server marks this deferred
    // response as sent. Close the underlying connection on later failures.
    if (!response_) { request->client()->close(); return; }
    response_->addHeader("Cache-Control", "no-store");
    response_->_respond(request);
  }
};
#endif
