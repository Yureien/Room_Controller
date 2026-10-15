#include "web_dispatch.h"
#include <atomic>
#include <cassert>
#include <string>
#include <thread>
#include <vector>

int main() {
  WebDispatch dispatch;
  WebDispatch::Input read, command;
  command.kind = WebDispatch::Kind::AcCommand;
  assert(!dispatch.submit(command, 0));
  dispatch.setOnline(true);
  std::vector<WebDispatch::Token> reads;
  for (size_t i = WebDispatch::kCommandSlots; i < WebDispatch::kSlots; ++i) {
    reads.push_back(dispatch.submit(read, 0));
    assert(reads.back());
  }
  assert(!dispatch.submit(read, 0));
  auto first = dispatch.submit(command, 0);
  auto second = dispatch.submit(command, 0);
  assert(first && second && !dispatch.submit(command, 0));
  WebDispatch::Work work;
  assert(dispatch.claim(1, work) && work.token.id == first.id);
  dispatch.cancel(first);  // Disconnect while main loop is executing.
  auto replacement = dispatch.submit(command, 1);
  dispatch.complete(first, 200, "stale", 5);
  assert(!dispatch.consume(replacement, 2, [](int, const char*) { assert(false); }));
  assert(dispatch.claim(2, work) && work.token.id == second.id);  // FIFO commands.
  dispatch.complete(second, 400, "bad", 3);
  assert(dispatch.consume(second, 2, [](int code, const char* body) {
    assert(code == 400 && std::string(body) == "bad");
  }));
  dispatch.cancel(first);  // A late old destructor must not cancel its replacement.
  assert(dispatch.claim(2, work) && work.token.id == replacement.id);
  dispatch.complete(replacement, 202, "accepted", 8);
  assert(dispatch.consume(replacement, 2, [](int code, const char* body) {
    assert(code == 202 && std::string(body) == "accepted");
  }));
  dispatch.cancel(reads[0]);  // Cancel before execution: never claimed.
  assert(dispatch.claim(2, work) && work.token.id == reads[1].id);
  std::string oversized(WebDispatch::kResponseSize, 'x');
  dispatch.complete(work.token, 200, oversized.c_str(), oversized.size());
  assert(dispatch.consume(work.token, 2, [](int code, const char* body) {
    assert(code == 503 && std::string(body).find("Response too large") != std::string::npos);
  }));
  dispatch.setOnline(false);
  assert(!dispatch.claim(3, work));
  for (size_t i = 2; i < reads.size(); ++i)
    assert(dispatch.consume(reads[i], 3, [](int code, const char*) { assert(code == 503); }));
  dispatch.setOnline(true);
  auto expired = dispatch.submit(command, UINT32_MAX - 1000);
  assert(!dispatch.claim(1000, work));  // Expiry across millis() rollover.
  assert(dispatch.consume(expired, 1000, [](int code, const char*) { assert(code == 503); }));

  // Exercise publication, cancellation, and slot reuse concurrently. A reply
  // must always belong to its token; neither thread ever borrows slot storage.
  std::atomic<bool> stop{false};
  std::thread mainLoop([&] {
    WebDispatch::Work job;
    while (!stop.load()) {
      if (dispatch.claim(0, job)) {
        const std::string value = std::to_string(job.token.id);
        dispatch.complete(job.token, 200, value.c_str(), value.size());
      } else std::this_thread::yield();
    }
  });
  for (unsigned i = 0; i < 10000; ++i) {
    auto token = dispatch.submit(i % 2 ? command : read, 0);
    assert(token);
    if (i % 3 == 0) dispatch.cancel(token);
    else {
      while (!dispatch.consume(token, 0, [&](int code, const char* body) {
        assert(code == 200 && body == std::to_string(token.id));
      })) std::this_thread::yield();
    }
  }
  stop.store(true);
  mainLoop.join();
}
