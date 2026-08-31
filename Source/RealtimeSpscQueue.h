#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace fiddle {

template <typename T, std::size_t Capacity> class RealtimeSpscQueue {
  static_assert(Capacity >= 2);
  static_assert(std::is_trivially_copyable_v<T>);

public:
  [[nodiscard]] bool tryPush(const T &value) noexcept {
    const auto write = writeIndex_.load(std::memory_order_relaxed);
    const auto next = increment(write);
    if (next == readIndex_.load(std::memory_order_acquire))
      return false;

    storage_[write] = value;
    writeIndex_.store(next, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool tryPop(T &value) noexcept {
    const auto read = readIndex_.load(std::memory_order_relaxed);
    if (read == writeIndex_.load(std::memory_order_acquire))
      return false;

    value = storage_[read];
    readIndex_.store(increment(read), std::memory_order_release);
    return true;
  }

  [[nodiscard]] constexpr std::size_t usableCapacity() const noexcept {
    return Capacity - 1;
  }

private:
  static constexpr std::size_t increment(std::size_t index) noexcept {
    return (index + 1) % Capacity;
  }

  std::array<T, Capacity> storage_{};
  alignas(64) std::atomic<std::size_t> writeIndex_{0};
  alignas(64) std::atomic<std::size_t> readIndex_{0};
};

} // namespace fiddle
