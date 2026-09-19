#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace fiddle {
// Analysis never executes on the renderer. Fixed-capacity SPSC audio queue;
// overrun invalidates the measurement until Reset instead of blocking audio.
// prepare()/stop() require a quiescent renderer; reset()/toJson() run on the
// message thread, submit() on the single Master render thread.
class MasterLoudnessMeter final : private juce::Thread {
public:
  MasterLoudnessMeter() : juce::Thread("Master loudness") {}
  ~MasterLoudnessMeter() override { stop(); }
  void prepare(double rate) {
    stop();
    rate_ = static_cast<unsigned long>(juce::jmax(1.0, rate));
    packets_ = std::make_unique<Packet[]>(capacity);
    fifo_.reset();
    dropped_.store(0);
    reset();
    if (!startThread(juce::Thread::Priority::low)) {
      failed_.store(true);
      published_.store(requested_.load(), std::memory_order_release);
    }
  }
  void stop() { signalThreadShouldExit(); notify(); stopThread(-1); }
  void reset() noexcept {
    droppedAtReset_.store(dropped_.load());
    requested_.fetch_add(1, std::memory_order_acq_rel);
  }
  void submit(const juce::AudioBuffer<float> &audio) noexcept {
    if (!packets_) return;
    const auto epoch = requested_.load(std::memory_order_acquire);
    for (int offset = 0; offset < audio.getNumSamples(); offset += packetFrames) {
      const int frames = juce::jmin(packetFrames, audio.getNumSamples() - offset);
      int start, count, unusedStart, unusedCount;
      fifo_.prepareToWrite(1, start, count, unusedStart, unusedCount);
      if (!count) { dropped_.fetch_add(audio.getNumSamples() - offset, std::memory_order_relaxed); return; }
      auto &packet = packets_[start];
      packet.epoch = epoch;
      packet.frames = frames;
      for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::copy(packet.audio[ch].data(), audio.getReadPointer(ch) + offset, frames);
      fifo_.finishedWrite(1);
    }
  }
  juce::var toJson() const {
    auto *result = new juce::DynamicObject();
    const auto requested = requested_.load(std::memory_order_acquire);
    const bool current = published_.load(std::memory_order_acquire) == requested;
    const auto dropped = dropped_.load(), baseline = droppedAtReset_.load();
    const auto lost = dropped >= baseline ? dropped - baseline : 0;
    const bool valid = current && !failed_.load() && lost == 0;
    result->setProperty("valid", valid);
    result->setProperty("incomplete", current && (lost != 0 || failed_.load()));
    result->setProperty("droppedFrames", static_cast<juce::int64>(lost));
    result->setProperty("seconds", current ? seconds_.load() : 0.0);
    const auto value = [&](double v) { return valid && std::isfinite(v) ? juce::var(v) : juce::var(); };
    result->setProperty("integrated", value(integrated_.load()));
    result->setProperty("shortTerm", value(shortTerm_.load()));
    result->setProperty("truePeakLeft", value(truePeak_[0].load()));
    result->setProperty("truePeakRight", value(truePeak_[1].load()));
    return juce::var(result);
  }
private:
  static constexpr int packetFrames = 256, capacity = 1024;
  struct Packet { uint64_t epoch = 0; int frames = 0; std::array<std::array<float, packetFrames>, 2> audio; };
  juce::AbstractFifo fifo_{capacity};
  std::unique_ptr<Packet[]> packets_;
  unsigned long rate_ = 48000;
  std::atomic<uint64_t> requested_{0}, published_{0}, dropped_{0}, droppedAtReset_{0};
  std::atomic<bool> failed_{false};
  std::atomic<double> integrated_{-INFINITY}, shortTerm_{-INFINITY}, seconds_{0};
  std::array<std::atomic<double>, 2> truePeak_{{-INFINITY, -INFINITY}};
  void run() override;
};
} // namespace fiddle
