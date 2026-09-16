#pragma once
#include "../AudioProcessingGate.h"
#include "../AudioDiagnostics.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

namespace fiddle {
// One coordinator, at most three persistent helpers. Only the render-ahead
// coordinator waits for completion; the native audio callback never enters
// this pool. Configuration/destruction require no block in flight.
class ParallelRenderPool {
public:
  using Task = void (*)(void *, size_t);
  static int validCount(int count) noexcept {
    return count == 2 || count == 4 ? count : 1;
  }
  ~ParallelRenderPool() { workers_.clear(); }

  void configure(int count, double rate, int blockSize) {
    workers_.clear();
    actualCount_.store(1);
    realtime_.store(true);
    const auto period = 1000.0 * blockSize / rate;
    const auto options = juce::Thread::RealtimeOptions{}.withPeriodMs(period)
        .withProcessingTimeMs(period * 0.75).withMaximumProcessingTimeMs(period);
    for (int i = 1; i < validCount(count); ++i) {
      auto worker = std::make_unique<Worker>(*this);
      if (!worker->startRealtimeThread(options)) {
        realtime_.store(false);
        if (!worker->startThread(juce::Thread::Priority::high)) {
          workers_.clear(); // Safe, explicit single-thread fallback.
          return;
        }
      }
      workers_.push_back(std::move(worker));
    }
    actualCount_.store(static_cast<int>(workers_.size()) + 1);
  }

  int count() const noexcept { return actualCount_.load(); }
  bool realtime() const noexcept { return realtime_.load(); }

  // Stack context and owner are retained until EVERY dispatched helper exits,
  // even if one participant consumed all jobs before the others woke up.
  double process(size_t size, void *context, Task task,
                 const AudioProcessingGate::Render &owner) {
    size_ = size;
    context_ = context;
    task_ = task;
    owner_ = &owner;
    next_.store(0, std::memory_order_relaxed);
    for (auto &worker : workers_) {
      worker->pending.store(true, std::memory_order_release);
      worker->notify();
    }
    runJobs(); // The coordinator does useful work too.
    double pluginWorkMs = 0;
    for (auto &worker : workers_) {
      worker->done.wait(-1);
      pluginWorkMs += worker->pluginWorkMs;
    }
    return pluginWorkMs;
  }

private:
  void runJobs() {
    for (;;) {
      const auto index = next_.fetch_add(1, std::memory_order_relaxed);
      if (index >= size_) return;
      task_(context_, index);
    }
  }
  struct Worker final : juce::Thread {
    explicit Worker(ParallelRenderPool &pool)
        : Thread("Fiddle layer render"), pool(pool) {}
    ~Worker() override {
      signalThreadShouldExit();
      notify();
      waitForThreadToExit(-1); // Never terminate a thread in vendor code.
    }
    void run() override {
      while (!threadShouldExit()) {
        wait(-1);
        if (!pending.exchange(false, std::memory_order_acquire)) continue;
        {
          AudioProcessingGate::RenderParticipant participant(*pool.owner_);
          PluginRenderDiagnostics::beginBlock();
          pool.runJobs();
          pluginWorkMs = PluginRenderDiagnostics::blockWorkMs();
        }
        done.signal();
      }
    }
    ParallelRenderPool &pool;
    std::atomic<bool> pending{false};
    juce::WaitableEvent done;
    double pluginWorkMs = 0;
  };
  std::vector<std::unique_ptr<Worker>> workers_;
  std::atomic<int> actualCount_{1};
  std::atomic<bool> realtime_{true};
  std::atomic<size_t> next_{0};
  size_t size_ = 0;
  void *context_ = nullptr;
  Task task_ = nullptr;
  const AudioProcessingGate::Render *owner_ = nullptr;
};
} // namespace fiddle
