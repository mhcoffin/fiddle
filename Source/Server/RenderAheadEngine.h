#pragma once
#include "../AudioDiagnostics.h"
#include "../AudioStreamRing.h"
#include "../RealtimeObjectPublisher.h"
#include "RealtimeMixPrinter.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace fiddle {
class MixerModel;
// A single block coordinator, optionally assisted by the mixer's layer workers.
// Device lifecycle joins it (and thus each block's helpers) before re-preparing
// any processors; the CoreAudio callback only clears its unused output.
class RenderAheadEngine final : private juce::Thread {
public:
  RenderAheadEngine(MixerModel &, AudioRenderDiagnostics &, juce::File streamFile);
  ~RenderAheadEngine() override;
  juce::String start(double rate, int blockSize);
  void stop();
  juce::String startMixPrint(const juce::File &file);
  bool beginMixPrintAt(double presentationTimeMs) noexcept;
  bool endMixPrintAt(double presentationTimeMs) noexcept;
  void stopMixPrint();
  bool mixPrintIsArmed() const noexcept;
  bool mixPrintNeedsFinalizing(double currentPresentationTimeMs) const noexcept;
  juce::var mixPrintState() const;
  uint64_t streamId() const noexcept;
  double reserveMs() const noexcept;
  juce::var diagnostics() const;
private:
  struct Stream {
    std::unique_ptr<juce::MemoryMappedFile> mapping;
    AudioStreamRing *ring = nullptr;
    juce::AudioBuffer<float> scratch;
    std::atomic<uint64_t> renderedBlocks{0}, skippedFrames{0}, clockJumps{0};
    // Actual worker retry sleeps after a denied render, not control duration
    // (which can overlap an already-running block). Reset with each stream.
    std::atomic<uint64_t> controlWaitCount{0}, controlWaitUs{0};
    ~Stream() { if (ring) ring->active.store(0, std::memory_order_release); }
  };
  void run() override;
  MixerModel &mixer_;
  AudioRenderDiagnostics &recorder_;
  juce::File file_;
  RealtimeObjectPublisher<Stream> stream_;
  std::atomic<bool> realtime_{false};
  RealtimeMixPrinter mixPrinter_;
};
} // namespace fiddle
