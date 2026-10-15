#ifndef WEB_DISPATCH_H_
#define WEB_DISPATCH_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

// The only mutable state shared by loop() and the AsyncTCP task. No HTTP
// request, controller object, String, or ring-buffer pointer crosses this bridge.
class WebDispatch {
 public:
  static constexpr size_t kCommandSlots = 2, kSlots = 6;
  static constexpr size_t kResponseSize = 3072;
  static constexpr uint32_t kQueueTimeoutMs = 2000;
  enum class Kind { State, History, AcCommand, LampCommand };
  struct Input {
    Kind kind = Kind::State;
    char command[128]{};
    uint64_t cursor = 0;
    uint16_t limit = 128;
    bool reset = true;
  };
  struct Token {
    size_t slot = kSlots;
    uint64_t id = 0;
    explicit operator bool() const { return slot < kSlots; }
  };
  struct Work { Token token; Input input; };

  // Main-loop lifecycle. Already accepted work is rejected on Wi-Fi loss.
  void setOnline(bool online) {
    std::lock_guard<std::mutex> lock(mutex_);
    online_ = online;
    if (!online) for (auto& slot : slots_)
      if (slot.state == State::Queued || slot.state == State::Processing)
        fail(slot, "Controller disconnected. Check state before retrying.");
  }

  Token submit(const Input& input, uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!online_) return {};
    const bool command = isCommand(input.kind);
    for (size_t i = command ? 0 : kCommandSlots;
         i < (command ? kCommandSlots : kSlots); ++i) {
      Slot& slot = slots_[i];
      if (slot.state != State::Free) continue;
      slot.input = input;
      slot.queuedAt = now;
      slot.id = ++sequence_;
      slot.state = State::Queued;
      Token token;
      token.slot = i;
      token.id = slot.id;
      return token;
    }
    return {};
  }

  // Claim at most one item per loop; commands have reserved slots and priority.
  bool claim(uint32_t now, Work& work) {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* chosen = nullptr;
    size_t index = 0;
    for (size_t i = 0; i < kSlots; ++i) {
      Slot& slot = slots_[i];
      if (slot.state != State::Queued) continue;
      if (now - slot.queuedAt >= kQueueTimeoutMs) {
        fail(slot, "Request expired before execution. Check state before retrying.");
        continue;
      }
      if (!chosen || (isCommand(slot.input.kind) && !isCommand(chosen->input.kind)) ||
          (isCommand(slot.input.kind) == isCommand(chosen->input.kind) && slot.id < chosen->id)) {
        chosen = &slot;
        index = i;
      }
    }
    if (!chosen) return false;
    chosen->state = State::Processing;
    work.token.slot = index;
    work.token.id = chosen->id;
    work.input = chosen->input;
    return true;
  }

  // Publish a bounded immutable snapshot. A cancelled/reused token cannot
  // publish into another request's slot, even if cancellation happens mid-IR.
  void complete(Token token, int code, const char* body, size_t length) {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = find(token);
    if (!slot || slot->state != State::Processing) return;
    if (length >= kResponseSize) {
      fail(*slot, "Response too large. Check state before retrying.");
      return;
    }
    std::memcpy(slot->body, body, length);
    slot->body[length] = '\0';
    slot->code = code;
    slot->state = State::Ready;
  }

  // Network task only. The consumer copies the snapshot into a library-owned
  // response before releasing the slot. It must not re-enter this bridge.
  template<class Consumer>
  bool consume(Token token, uint32_t now, Consumer consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = find(token);
    if (!slot) return false;
    if (slot->state == State::Queued && now - slot->queuedAt >= kQueueTimeoutMs)
      fail(*slot, "Request expired before execution. Check state before retrying.");
    if (slot->state != State::Ready) return false;
    consumer(slot->code, slot->body);
    slot->state = State::Free;
    return true;
  }

  void cancel(Token token) {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = find(token);
    if (slot) slot->state = State::Free;
  }

 private:
  enum class State { Free, Queued, Processing, Ready };
  struct Slot {
    State state = State::Free;
    uint64_t id = 0;
    Input input;
    uint32_t queuedAt = 0;
    int code = 200;
    char body[kResponseSize]{};
  };
  std::mutex mutex_;
  Slot slots_[kSlots];
  uint64_t sequence_ = 0;
  bool online_ = false;
  static bool isCommand(Kind kind) {
    return kind == Kind::AcCommand || kind == Kind::LampCommand;
  }
  Slot* find(Token token) {
    if (!token || slots_[token.slot].id != token.id ||
        slots_[token.slot].state == State::Free) return nullptr;
    return &slots_[token.slot];
  }
  static void fail(Slot& slot, const char* message) {
    const char prefix[] = "{\"ok\":false,\"message\":\"";
    const size_t size = sizeof(prefix) - 1;
    std::memcpy(slot.body, prefix, size);
    const size_t length = std::strlen(message);
    std::memcpy(slot.body + size, message, length);
    std::memcpy(slot.body + size + length, "\"}", 3);
    slot.code = 503;
    slot.state = State::Ready;
  }
};
static_assert(sizeof(WebDispatch) <= 20 * 1024, "Web handoff RAM budget exceeded");
#endif
