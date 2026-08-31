#pragma once

#include <atomic>
#include <cstdint>

namespace fiddle {

/// Pins the current value of a published pointer for a real-time reader.
/// Writers exchange the pointer first, retire the old object, and reclaim
/// retired objects only while the reader count is zero. Publication/retirement
/// and the zero-check/reclamation must share a non-real-time writer mutex so a
/// publication cannot slip between the check and deletion.
template <typename T> class RealtimeReadGuard {
public:
  RealtimeReadGuard(const std::atomic<T *> &published,
                    std::atomic<uint32_t> &readers) noexcept
      : readers_(readers) {
    readers_.fetch_add(1, std::memory_order_acquire);
    value_ = published.load(std::memory_order_acquire);
  }

  ~RealtimeReadGuard() {
    readers_.fetch_sub(1, std::memory_order_release);
  }

  RealtimeReadGuard(const RealtimeReadGuard &) = delete;
  RealtimeReadGuard &operator=(const RealtimeReadGuard &) = delete;

  [[nodiscard]] T *get() const noexcept { return value_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return value_ != nullptr;
  }

private:
  std::atomic<uint32_t> &readers_;
  T *value_ = nullptr;
};

} // namespace fiddle
