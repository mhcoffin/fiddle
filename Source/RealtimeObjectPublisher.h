#pragma once

#include "RealtimeReadGuard.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace fiddle {

/// Publishes immutable object generations to a real-time reader. Replaced
/// generations are reclaimed by the writer only after every read guard has
/// left. publish() and reclamation share one mutex, closing the otherwise
/// subtle zero-readers/publication race.
template <typename T> class RealtimeObjectPublisher {
public:
  using ReadGuard = RealtimeReadGuard<T>;

  ~RealtimeObjectPublisher() {
    assert(readerCount() == 0);
    delete active_.exchange(nullptr, std::memory_order_seq_cst);
  }

  RealtimeObjectPublisher() = default;
  RealtimeObjectPublisher(const RealtimeObjectPublisher &) = delete;
  RealtimeObjectPublisher &operator=(const RealtimeObjectPublisher &) = delete;

  [[nodiscard]] ReadGuard read() const noexcept {
    return ReadGuard(active_, readers_);
  }

  /// The writer may inspect the current generation outside the audio callback.
  [[nodiscard]] T *activeForWriter() const noexcept {
    return active_.load(std::memory_order_seq_cst);
  }

  void publish(std::unique_ptr<T> replacement) {
    std::lock_guard<std::mutex> lock(writerMutex_);
    auto *old =
        active_.exchange(replacement.release(), std::memory_order_seq_cst);
    if (old)
      retired_.emplace_back(old);
  }

  /// Returns false while a reader is active. Cleanup runs off the real-time
  /// thread immediately before each retired object is destroyed.
  template <typename Cleanup> bool reclaimRetired(Cleanup cleanup) {
    return reclaimRetiredWith([] {}, std::move(cleanup));
  }

  bool reclaimRetired() {
    return reclaimRetired([](T &) {});
  }

  /// Runs onQuiescent under the same writer lock as publication. This supports
  /// dependent objects (for example strips referenced by retired graphs) that
  /// must cross the same reader quiescence boundary.
  template <typename Quiescent, typename Cleanup>
  bool reclaimRetiredWith(Quiescent onQuiescent, Cleanup cleanup) {
    std::vector<std::unique_ptr<T>> reclaimable;
    {
      std::lock_guard<std::mutex> lock(writerMutex_);
      if (readers_.load(std::memory_order_seq_cst) != 0)
        return false;
      onQuiescent();
      reclaimable.swap(retired_);
    }
    for (auto &object : reclaimable)
      cleanup(*object);
    return true;
  }

  [[nodiscard]] uint32_t readerCount() const noexcept {
    return readers_.load(std::memory_order_seq_cst);
  }

private:
  std::atomic<T *> active_{nullptr};
  mutable std::atomic<uint32_t> readers_{0};
  mutable std::mutex writerMutex_;
  std::vector<std::unique_ptr<T>> retired_;
};

} // namespace fiddle
