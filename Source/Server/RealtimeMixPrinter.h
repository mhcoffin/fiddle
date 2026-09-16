#pragma once

#include <atomic>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace fiddle {

/// Copies final stereo Master blocks into a bounded FIFO and writes them to a
/// 32-bit floating-point WAV on a low-priority background thread. push() is
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
                     int maximumBlockSize);

  /// Opens and closes the capture gate at final-output presentation times.
  /// These are lock-free and may be called from the MIDI relay thread.
  bool beginAt(double presentationTimeMs) noexcept;
  bool endAt(double presentationTimeMs) noexcept;

  /// Stops accepting blocks, drains the background FIFO, and finalizes WAV
  /// headers. Safe to call when already stopped.
  void stop();

  /// Realtime entry point. A false return while recording means the disk FIFO
  /// was full and this block was counted as dropped.
  bool push(const juce::AudioBuffer<float> &audio,
            double blockPresentationTimeMs) noexcept;

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
  juce::String lastError_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RealtimeMixPrinter)
};

} // namespace fiddle
