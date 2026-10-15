"""Exercise the real routing/dispatch/response bridge with a fake HTTP transport."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/main.cpp").read_text()
names = ["serviceWebRequests", "sendWebError", "enqueueWebRequest",
         "enqueueWebCommand", "configureWebServer"]
functions = []
for name in names:
    matches = re.findall(r"^void " + name + r"\([^;]*?\) \{[\s\S]*?^\}", source, re.MULTILINE)
    assert len(matches) == 1, name
    functions.append(matches[0])

transport = r'''
#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
using String = std::string;
extern std::thread::id networkThread;
uint32_t millis();
inline void networkOnly() { assert(std::this_thread::get_id() == networkThread); }
class AsyncWebServerRequest;
class AsyncWebServerResponse {
 public:
  int code = 0;
  String body;
  bool started = false, finished = false;
  std::map<String, String> headers;
  virtual ~AsyncWebServerResponse() = default;
  virtual bool _sourceValid() const { return true; }
  virtual bool _started() const { return started; }
  virtual bool _finished() const { return finished; }
  virtual bool _failed() const { return false; }
  virtual void _respond(AsyncWebServerRequest*);
  virtual size_t _ack(AsyncWebServerRequest*, size_t, uint32_t);
  void addHeader(const char* key, const char* value) { headers[key] = value; }
};
struct AsyncWebParameter {
  String content;
  const String& value() const { return content; }
};
class AsyncWebServerRequest {
 public:
  std::map<String, AsyncWebParameter> query, post;
  String origin, hostname = "room.local", wireBody;
  int wireCode = 0;
  size_t window = 10000;
  bool aborted = false;
  std::unique_ptr<AsyncWebServerResponse> response;
  const String& header(const char*) const { networkOnly(); return origin; }
  const String& host() const { networkOnly(); return hostname; }
  const AsyncWebParameter* getParam(const char* key, bool form = false) const {
    networkOnly();
    const auto& params = form ? post : query;
    auto it = params.find(key);
    return it == params.end() ? nullptr : &it->second;
  }
  AsyncWebServerResponse* beginResponse(int code, const char*, const String& body) {
    networkOnly();
    auto* result = new AsyncWebServerResponse;
    result->code = code; result->body = body;
    return result;
  }
  AsyncWebServerResponse* beginResponse(int code, const char* type, const uint8_t* body, size_t size) {
    return beginResponse(code, type, String(reinterpret_cast<const char*>(body), size));
  }
  void send(AsyncWebServerResponse* next) { networkOnly(); response.reset(next); }
  void send(int code, const char* type = "", const char* body = "") { send(beginResponse(code, type, body)); }
  void abort() { networkOnly(); aborted = true; }
  void poll() { networkOnly(); response->_ack(this, 0, 0); }
};
inline void AsyncWebServerResponse::_respond(AsyncWebServerRequest* request) {
  networkOnly(); started = true; request->wireCode = code; _ack(request, 0, 0);
}
inline size_t AsyncWebServerResponse::_ack(AsyncWebServerRequest* request, size_t, uint32_t) {
  networkOnly();
  const size_t count = std::min(request->window, body.size() - request->wireBody.size());
  request->wireBody.append(body, request->wireBody.size(), count);
  finished = request->wireBody.size() == body.size();
  return count;
}
constexpr int HTTP_GET = 0, HTTP_POST = 1;
class AsyncWebServer {
 public:
  std::map<String, std::function<void(AsyncWebServerRequest*)>> routes;
  explicit AsyncWebServer(int) {}
  void on(const char* path, int, std::function<void(AsyncWebServerRequest*)> callback) { routes[path] = callback; }
  void onNotFound(std::function<void(AsyncWebServerRequest*)>) {}
  void dispatch(const char* path, AsyncWebServerRequest& request) {
    networkOnly(); routes.at(path)(&request); request.response->_respond(&request);
  }
};
'''

fixture = r'''
#include "ESPAsyncWebServer.h"
#include "temperature_history_api.h"
#include "web_response.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>
#define F(value) value
std::thread::id ownerThread = std::this_thread::get_id(), networkThread;
std::atomic<uint32_t> clockMs{0};
uint32_t millis() { return clockMs.load(); }
uint64_t esp_timer_get_time() { return uint64_t(millis()) * 1000; }
uint32_t esp_random() { return 0x1234abcd; }
void mainOnly() { assert(std::this_thread::get_id() == ownerThread); }
void boostCpu() { mainOnly(); }
std::atomic<bool> webActivity{false};
WebDispatch webDispatch;
AsyncWebServer server(80);
TemperatureHistory temperatureHistory;
char temperatureHistorySession[9];
const char kControlPage[] = "static page with 100% CSS";
String jsonString(const String& value) { return "\"" + value + "\""; }
String stateJson(const String& message = "", bool ok = true) {
  mainOnly();
  return String("{\"power\":true,\"ok\":") + (ok ? "true" : "false") + ",\"message\":" + jsonString(message) + "}";
}
struct SerialStub { void print(const char*) {} void println(const char*) {} } Serial;
void printState() { mainOnly(); }
std::vector<String> commands;
std::function<void()> duringCommand;
bool executeAcCommand(const String& command, String* message) {
  mainOnly(); commands.push_back(command); *message = command;
  if (duringCommand) { auto hook = std::move(duringCommand); hook(); }
  return command != "bad";
}
struct LampStub {
  bool command(const String& command, bool, String& message) { mainOnly(); message = command; return command != "bad"; }
} lamp;
void onNetwork(std::function<void()> action) {
  std::thread thread([&] { networkThread = std::this_thread::get_id(); action(); });
  thread.join();
}
'''

checks = r'''
int main() {
  configureWebServer(); webDispatch.setOnline(true);
  AsyncWebServerRequest ac, lampRequest;
  ac.post["command"].content = "temp 25";
  lampRequest.post["command"].content = "color red";
  onNetwork([&] { server.dispatch("/api/command", ac); });
  assert(commands.empty() && !ac.response->_started());
  serviceWebRequests();
  assert(commands.size() == 1 && ac.wireCode == 0);
  onNetwork([&] { ac.poll(); });
  assert(ac.wireCode == 200 && ac.wireBody.find("temp 25") != String::npos);
  onNetwork([&] { server.dispatch("/api/lamp/command", lampRequest); });
  serviceWebRequests(); onNetwork([&] { lampRequest.poll(); });
  assert(lampRequest.wireCode == 202);
  AsyncWebServerRequest invalid, crossOrigin, queryCommand;
  invalid.post["command"].content = "bad";
  crossOrigin.post["command"].content = "off";
  crossOrigin.origin = "http://other.local";
  queryCommand.query["command"].content = "off";
  onNetwork([&] {
    server.dispatch("/api/command", invalid);
    server.dispatch("/api/command", crossOrigin);
    server.dispatch("/api/command", queryCommand);
  });
  assert(crossOrigin.wireCode == 403 && queryCommand.wireCode == 400);
  serviceWebRequests(); onNetwork([&] { invalid.poll(); });
  assert(invalid.wireCode == 400 && commands.back() == "bad");

  // Filling every read slot cannot consume reserved command capacity.
  std::vector<std::unique_ptr<AsyncWebServerRequest>> reads;
  for (unsigned i = 0; i < 4; ++i) {
    reads.emplace_back(new AsyncWebServerRequest);
    onNetwork([&] { server.dispatch("/api/temperature/history", *reads.back()); });
  }
  AsyncWebServerRequest overloaded, priority;
  priority.post["command"].content = "off";
  onNetwork([&] { server.dispatch("/api/state", overloaded); server.dispatch("/api/command", priority); });
  assert(overloaded.wireCode == 503);
  serviceWebRequests();
  assert(commands.back() == "off");
  onNetwork([&] { priority.poll(); });
  assert(priority.wireCode == 200);

  // A history snapshot remains consistent while the ring changes and the
  // original client drains slowly; later commands still execute.
  temperatureHistory.record(0, true, 20);
  serviceWebRequests();
  clockMs = 5000;
  temperatureHistory.record(5, true, 30);
  reads[0]->window = 3;
  onNetwork([&] { reads[0]->poll(); });
  assert(!reads[0]->response->_finished());
  AsyncWebServerRequest later;
  later.post["command"].content = "on";
  onNetwork([&] { server.dispatch("/api/command", later); });
  serviceWebRequests(); onNetwork([&] { later.poll(); });
  assert(later.wireCode == 200 && commands.back() == "on");
  onNetwork([&] { while (!reads[0]->response->_finished()) reads[0]->poll(); });
  assert(reads[0]->wireBody.find("\"samples\":[20.00]") != String::npos);
  onNetwork([&] { reads.clear(); });  // Cancel remaining unclaimed reads.

  const size_t beforeCancel = commands.size();
  std::unique_ptr<AsyncWebServerRequest> cancelled(new AsyncWebServerRequest);
  cancelled->post["command"].content = "off";
  onNetwork([&] { server.dispatch("/api/command", *cancelled); cancelled.reset(); });
  serviceWebRequests(); assert(commands.size() == beforeCancel);

  // Disconnect and reuse the same slot while the main loop executes an IR
  // command. Its old completion/destructor cannot affect the replacement.
  std::unique_ptr<AsyncWebServerRequest> inFlight(new AsyncWebServerRequest);
  inFlight->post["command"].content = "temp 24";
  AsyncWebServerRequest replacement;
  replacement.post["command"].content = "temp 26";
  onNetwork([&] { server.dispatch("/api/command", *inFlight); });
  duringCommand = [&] { onNetwork([&] {
    inFlight.reset(); server.dispatch("/api/command", replacement);
  }); };
  serviceWebRequests(); serviceWebRequests();
  onNetwork([&] { replacement.poll(); });
  assert(replacement.wireBody.find("temp 26") != String::npos);
  assert(replacement.wireBody.find("temp 24") == String::npos);

  AsyncWebServerRequest stale, disconnected;
  stale.post["command"].content = "off";
  onNetwork([&] { server.dispatch("/api/command", stale); });
  const size_t beforeExpire = commands.size();
  clockMs += 2000; serviceWebRequests(); onNetwork([&] { stale.poll(); });
  assert(stale.wireCode == 503 && commands.size() == beforeExpire);
  disconnected.post["command"].content = "off";
  onNetwork([&] { server.dispatch("/api/command", disconnected); });
  webDispatch.setOnline(false); serviceWebRequests();
  onNetwork([&] { disconnected.poll(); });
  assert(disconnected.wireCode == 503 && commands.size() == beforeExpire);
  webDispatch.setOnline(true);

  AsyncWebServerRequest malformed, restart, delta, page;
  malformed.query["limit"].content = "129";
  restart.query["session"].content = "deadbeef";
  restart.query["cursor"].content = "1";
  delta.query["session"].content = "1234abcd";
  delta.query["cursor"].content = "1";
  onNetwork([&] {
    server.dispatch("/api/temperature/history", malformed);
    server.dispatch("/api/temperature/history", restart);
    server.dispatch("/api/temperature/history", delta);
    server.dispatch("/", page);
  });
  assert(malformed.wireCode == 400 && page.wireBody == kControlPage);
  serviceWebRequests(); serviceWebRequests();
  onNetwork([&] { restart.poll(); delta.poll(); });
  assert(restart.wireBody.find("\"reset\":true") != String::npos);
  assert(delta.wireBody.find("\"samples\":[30.00]") != String::npos);
}
'''

with tempfile.TemporaryDirectory() as directory:
    temp = pathlib.Path(directory)
    (temp / "ESPAsyncWebServer.h").write_text(transport)
    (temp / "test.cpp").write_text(fixture + "\n".join(functions) + checks)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "-I" + str(temp), "-I" + str(ROOT / "src"), str(temp / "test.cpp"),
                    "-o", str(temp / "test")], check=True)
    subprocess.run([str(temp / "test")], check=True)
print("Async web checks passed: routing, task ownership, overload, snapshots, disconnects, and expiry.")
