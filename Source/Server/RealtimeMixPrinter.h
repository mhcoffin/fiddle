#pragma once

#include <atomic>
#include <memory>
#include "BusPrintCapture.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace fiddle {

/// Copies Master and/or selected bus blocks into one bounded FIFO and writes
/// synchronized 32-bit floating-point WAVs on a low-priority thread. push() is
/// called only by the single render-ahead worker and never performs file I/O
/// or waits.
class RealtimeMixPrinter final {
public:
  RealtimeMixPrinter();
  ~RealtimeMixPrinter();

  /// Arms a new print, replacing an existing file only after the user has
  /// selected it through an overwrite-confirming file chooser. No samples are
  /// written until beginAt() receives Dorico's transport-start boundary.
  juce::String start(const juce::File &file, double sampleRate,
                     int maximumBlockSize, double tailSeconds = 0.0);

  struct Stem { juce::String id, name; };
  /// Creates a new uniquely named take folder; never replaces existing stems.
  juce::String startStems(const juce::File &parentFolder, double sampleRate,
                          int maximumBlockSize, const std::vector<Stem> &buses,
                          bool includeMaster, int masterLatencySamples,
                          double tailSeconds = 0.0);

  /// Holds capture storage alive across rendering, even if the UI stops the
  /// print concurrently. The callback always renders exactly once.
  template <typename Render>
  void renderAndPush(juce::AudioBuffer<float> &master, double time,
                     uint64_t startFrame, Render render) {
    activeWrites_.fetch_add(1, std::memory_order_seq_cst);
    struct Release {
      std::atomic<unsigned> &writes;
      ~Release() { writes.fetch_sub(1, std::memory_order_seq_cst); }
    } release{activeWrites_};
    const bool active = activeWriter_.load(std::memory_order_seq_cst) != nullptr &&
                        !finalizing_.load() && failure_.load() == 0;
    if (active) capture_.clear();
    render(active && !capture_.buses.empty() ? &capture_ : nullptr);
    if (active) {
      if (capture_.latencyChanged) failRealtime(2);
      for (const auto &bus : capture_.buses)
        if (!bus->found) failRealtime(3);
      if (failure_.load() == 0) {
        combined_.clear();
        int channel = 0;
        if (includeMaster_) {
          for (int ch = 0; ch < 2; ++ch)
            combined_.copyFrom(ch, 0, master, ch, 0, master.getNumSamples());
          channel = 2;
        }
        for (auto &bus : capture_.buses) {
          float *channels[]{combined_.getWritePointer(channel), combined_.getWritePointer(channel + 1)};
          juce::AudioBuffer<float> target(channels, 2, master.getNumSamples());
          bus->delay.addTo(target, bus->audio, master.getNumSamples(), 1.0f);
          channel += 2;
        }
        // Avoid a temporary multichannel AudioBuffer view: JUCE allocates its
        // channel-pointer table when a view has more than 32 channels.
        (void)push(combined_, time, startFrame, master.getNumSamples());
      }
    }
  }

  /// Opens the capture gate and schedules its close after the configured tail
  /// at final-output presentation times. stop() still finishes immediately.
  /// These are lock-free and may be called from the MIDI relay thread.
  bool beginAt(double presentationTimeMs) noexcept;
  bool endAt(double presentationTimeMs) noexcept;

  /// Stops accepting blocks, drains the background FIFO, and finalizes WAV
  /// headers. Safe to call when already stopped.
  void stop();

  /// Realtime entry point for already assembled channels. Production calls it
  /// inside renderAndPush's lifetime guard. Failure ends the entire take.
  /// startFrame is the stream's integer sample cursor, NOT a wall-clock time.
  /// Presentation time gates transport boundaries; frame positions detect loss.
  bool push(const juce::AudioBuffer<float> &audio,
            double blockPresentationTimeMs, uint64_t startFrame,
            int sampleCount = -1) noexcept;

  [[nodiscard]] bool isArmed() const noexcept {
    return armed_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool isRecording() const noexcept {
    return recording_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool
  needsFinalizing(double currentPresentationTimeMs) const noexcept;
  [[nodiscard]] juce::var state() const;

private:
  juce::String open(const std::vector<juce::File> &files, double sampleRate,
                     int maximumBlockSize, bool replace, double tailSeconds);
  void failRealtime(int reason) noexcept;
  juce::TimeSliceThread writerThread_{"Fiddle mix print writer"};
  std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer_;
  std::atomic<juce::AudioFormatWriter::ThreadedWriter *> activeWriter_{nullptr};
  std::atomic<unsigned> activeWrites_{0};
  std::atomic<uint64_t> acceptedSamples_{0};
  std::atomic<uint64_t> droppedBlocks_{0};
  std::atomic<uint64_t> droppedSamples_{0};
  std::atomic<bool> armed_{false};
  std::atomic<bool> recording_{false};
  std::atomic<bool> finalizing_{false};
  std::atomic<double> startTimeMs_{0.0};
  std::atomic<double> endTimeMs_{0.0};
  juce::File file_;
  double sampleRate_ = 0.0;
  double tailSeconds_ = 0.0; // immutable while armed
  juce::String lastError_;
  BusPrintCapture capture_;
  juce::AudioBuffer<float> combined_;
  std::vector<const float *> writePointers_;
  std::vector<juce::File> files_;
  bool includeMaster_ = true;
  // 1 disk write, 2 latency changed, 3 selected bus removed, 4 queue overflow,
  // 5 sample stream discontinuity. All make the entire take incomplete.
  std::atomic<int> failure_{0};
  uint64_t expectedNextFrame_ = 0; // render coordinator only

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RealtimeMixPrinter)
};

} // namespace fiddle
