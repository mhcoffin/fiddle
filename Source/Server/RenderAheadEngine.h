#pragma once
#include "../AudioDiagnostics.h"
#include "../AudioStreamRing.h"
#include "../RealtimeObjectPublisher.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace fiddle {
class MixerModel;
// A single DSP owner. Device lifecycle stops/joins it before re-preparing any
// processors; the CoreAudio callback itself only clears its unused output.
class RenderAheadEngine final : private juce::Thread {
public:
  RenderAheadEngine(MixerModel &, AudioRenderDiagnostics &, juce::File streamFile);
  ~RenderAheadEngine() override;
  juce::String start(double rate, int blockSize);
  void stop();
  uint64_t streamId() const noexcept;
  double reserveMs() const noexcept;
  juce::var diagnostics() const;
private:
  struct Stream {
    std::unique_ptr<juce::MemoryMappedFile> mapping;
    AudioStreamRing *ring = nullptr;
    juce::AudioBuffer<float> scratch;
    std::atomic<uint64_t> renderedBlocks{0}, skippedFrames{0}, clockJumps{0};
    ~Stream() { if (ring) ring->active.store(0, std::memory_order_release); }
  };
  void run() override;
  MixerModel &mixer_;
  AudioRenderDiagnostics &recorder_;
  juce::File file_;
  RealtimeObjectPublisher<Stream> stream_;
  std::atomic<bool> realtime_{false};
};
} // namespace fiddle
