#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

namespace fiddle {
// Process-wide host lifecycle boundary. Control work may wait for an existing
// render to finish. Rendering NEVER waits: the worker retries without publishing
// a block, so the existing reserve covers short state/lifecycle operations.
// Nesting lets the mixer, strips and standalone hosted-slot tests share policy.
class AudioProcessingGate {
  static_assert(std::atomic<bool>::is_always_lock_free);
  static_assert(std::atomic<unsigned>::is_always_lock_free);
  inline static std::atomic<bool> requested_{false};
  inline static std::atomic<unsigned> readers_{0};
  inline static std::recursive_mutex controlMutex_;
  inline static thread_local unsigned renderDepth_ = 0, controlDepth_ = 0;
  inline static thread_local std::chrono::steady_clock::time_point controlStart_;
  inline static std::atomic<uint64_t> controlCount_{0}, longestControlUs_{0};
public:
  class Render {
    bool entered_ = false;
  public:
    Render() noexcept {
      if (renderDepth_ != 0) { ++renderDepth_; entered_ = true; return; }
      if (requested_.load(std::memory_order_seq_cst)) return;
      readers_.fetch_add(1, std::memory_order_seq_cst);
      if (requested_.load(std::memory_order_seq_cst)) {
        readers_.fetch_sub(1, std::memory_order_seq_cst); return;
      }
      ++renderDepth_; entered_ = true;
    }
    ~Render() { if (entered_ && --renderDepth_ == 0) readers_.fetch_sub(1, std::memory_order_seq_cst); }
    explicit operator bool() const noexcept { return entered_; }
    Render(const Render &) = delete;
  };
  class Control {
    std::unique_lock<std::recursive_mutex> lock_{controlMutex_, std::defer_lock};
  public:
    Control() {
      // Lifecycle APIs must never be invoked from a processor's render call.
      if (renderDepth_ != 0) std::terminate();
      lock_.lock();
      if (controlDepth_++ != 0) return;
      controlStart_ = std::chrono::steady_clock::now();
      requested_.store(true, std::memory_order_seq_cst);
      while (readers_.load(std::memory_order_seq_cst) != 0)
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    ~Control() {
      if (--controlDepth_ != 0) return;
      const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - controlStart_).count());
      controlCount_.fetch_add(1, std::memory_order_relaxed);
      if (elapsed > longestControlUs_.load()) longestControlUs_.store(elapsed);
      requested_.store(false, std::memory_order_seq_cst);
    }
    Control(const Control &) = delete;
  };
  static bool controlPending() noexcept { return requested_.load(std::memory_order_seq_cst); }
  static uint64_t controlCount() noexcept { return controlCount_.load(); }
  static double longestControlMs() noexcept { return longestControlUs_.load() / 1000.0; }
};
} // namespace fiddle
