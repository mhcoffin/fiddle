#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fiddle {

/// Bounded multi-producer/single-consumer queue for fixed-size real-time data.
/// Capacity must be a power of two. Producers and the consumer never allocate
/// or acquire a mutex; a full queue is reported to the caller.
template <typename T, std::size_t Capacity> class RealtimeMpscQueue {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two");
  static_assert(std::is_trivially_copyable_v<T>);

  struct Cell {
    std::atomic<std::size_t> sequence{0};
    T value{};
  };

public:
  RealtimeMpscQueue() noexcept {
    for (std::size_t i = 0; i < Capacity; ++i)
      cells_[i].sequence.store(i, std::memory_order_relaxed);
  }

  RealtimeMpscQueue(const RealtimeMpscQueue &) = delete;
  RealtimeMpscQueue &operator=(const RealtimeMpscQueue &) = delete;

  [[nodiscard]] bool tryPush(const T &value) noexcept {
    auto position = enqueuePosition_.load(std::memory_order_relaxed);
    for (;;) {
      auto &cell = cells_[position & kMask];
      const auto sequence = cell.sequence.load(std::memory_order_acquire);
      const auto difference = static_cast<std::intptr_t>(sequence) -
                              static_cast<std::intptr_t>(position);
      if (difference == 0) {
        if (enqueuePosition_.compare_exchange_weak(
                position, position + 1, std::memory_order_relaxed)) {
          cell.value = value;
          cell.sequence.store(position + 1, std::memory_order_release);
          return true;
        }
      } else if (difference < 0) {
        return false;
      } else {
        position = enqueuePosition_.load(std::memory_order_relaxed);
      }
    }
  }

  [[nodiscard]] bool tryPop(T &value) noexcept {
    const auto position = dequeuePosition_;
    auto &cell = cells_[position & kMask];
    const auto sequence = cell.sequence.load(std::memory_order_acquire);
    const auto difference = static_cast<std::intptr_t>(sequence) -
                            static_cast<std::intptr_t>(position + 1);
    if (difference != 0)
      return false;

    value = cell.value;
    dequeuePosition_ = position + 1;
    cell.sequence.store(position + Capacity, std::memory_order_release);
    return true;
  }

  [[nodiscard]] static constexpr std::size_t capacity() noexcept {
    return Capacity;
  }

private:
  static constexpr std::size_t kMask = Capacity - 1;
  std::array<Cell, Capacity> cells_{};
  alignas(64) std::atomic<std::size_t> enqueuePosition_{0};
  alignas(64) std::size_t dequeuePosition_ = 0;
};

} // namespace fiddle
