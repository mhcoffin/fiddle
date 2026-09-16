#include "RealtimeMixPrinter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fiddle {

RealtimeMixPrinter::RealtimeMixPrinter() = default;

RealtimeMixPrinter::~RealtimeMixPrinter() { stop(); }

juce::String RealtimeMixPrinter::start(const juce::File &file,
                                       double sampleRate,
                                       int maximumBlockSize) {
  stop();
  lastError_.clear();
  file_ = file;
  sampleRate_ = sampleRate;
  acceptedSamples_.store(0, std::memory_order_relaxed);
  droppedBlocks_.store(0, std::memory_order_relaxed);
  droppedSamples_.store(0, std::memory_order_relaxed);

  const auto fail = [this](const juce::String &message) {
    lastError_ = message;
    sampleRate_ = 0.0;
    return message;
  };

  if (file == juce::File{} || !std::isfinite(sampleRate) ||
      sampleRate < 8000.0 || sampleRate > 384000.0 || maximumBlockSize <= 0)
    return fail("Invalid print file or audio format");

  if (file.getParentDirectory().createDirectory().failed())
    return fail("Could not create the print destination folder");
  if (file.existsAsFile() && !file.deleteFile())
    return fail("Could not replace the selected WAV file");

  auto fileStream = file.createOutputStream();
  if (!fileStream || fileStream->failedToOpen())
    return fail("Could not open the selected WAV file for writing");
  std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);

  juce::WavAudioFormat wav;
  const auto options = juce::AudioFormatWriterOptions{}
                           .withSampleRate(sampleRate)
                           .withChannelLayout(
                               juce::AudioChannelSet::stereo())
                           .withBitsPerSample(32)
                           .withSampleFormat(
                               juce::AudioFormatWriterOptions::SampleFormat::
                                   floatingPoint);
  auto formatWriter = wav.createWriterFor(stream, options);
  if (!formatWriter)
    return fail("Could not create a 32-bit floating-point WAV writer");

  // Instrument streaming and the render-ahead worker must always win over
  // export I/O. The FIFO absorbs short low-priority scheduling delays; if the
  // disk still cannot keep up, write() reports a dropped block rather than
  // allowing the print thread to compete with Dorico's return path.
  if (!writerThread_.startThread(juce::Thread::Priority::low))
    return fail("Could not start the background mix-print writer");

  const auto eightSeconds = static_cast<int>(std::min(
      sampleRate * 8.0, static_cast<double>(std::numeric_limits<int>::max())));
  const auto fifoSamples = std::max(maximumBlockSize * 8, eightSeconds);
  writer_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
      formatWriter.release(), writerThread_, fifoSamples);
  // Do not periodically seek back and rewrite the WAV header during playback.
  // ThreadedWriter finalizes it when stop() drains and destroys the writer.
  activeWriter_.store(writer_.get(), std::memory_order_release);
  startTimeMs_.store(std::numeric_limits<double>::infinity(),
                     std::memory_order_relaxed);
  endTimeMs_.store(std::numeric_limits<double>::infinity(),
                   std::memory_order_relaxed);
  finalizing_.store(false, std::memory_order_relaxed);
  recording_.store(false, std::memory_order_relaxed);
  armed_.store(true, std::memory_order_release);
  return {};
}

bool RealtimeMixPrinter::beginAt(double presentationTimeMs) noexcept {
  if (!armed_.load(std::memory_order_acquire) ||
      finalizing_.load(std::memory_order_acquire) ||
      !std::isfinite(presentationTimeMs))
    return false;
  startTimeMs_.store(presentationTimeMs, std::memory_order_release);
  endTimeMs_.store(std::numeric_limits<double>::infinity(),
                   std::memory_order_release);
  recording_.store(true, std::memory_order_release);
  return true;
}

bool RealtimeMixPrinter::endAt(double presentationTimeMs) noexcept {
  if (!armed_.load(std::memory_order_acquire) ||
      !recording_.load(std::memory_order_acquire) ||
      !std::isfinite(presentationTimeMs))
    return false;
  endTimeMs_.store(presentationTimeMs, std::memory_order_release);
  return true;
}

bool RealtimeMixPrinter::needsFinalizing(
    double currentPresentationTimeMs) const noexcept {
  if (finalizing_.load(std::memory_order_acquire))
    return true;
  if (!armed_.load(std::memory_order_acquire) ||
      !recording_.load(std::memory_order_acquire) ||
      !std::isfinite(currentPresentationTimeMs))
    return false;
  const auto endTime = endTimeMs_.load(std::memory_order_acquire);
  // Dorico normally keeps calling process() after transport stop, allowing the
  // renderer to reach the exact boundary. If it suspends processing instead,
  // finalize shortly after that boundary rather than leave the WAV armed.
  return std::isfinite(endTime) && currentPresentationTimeMs >= endTime + 250.0;
}

void RealtimeMixPrinter::stop() {
  armed_.store(false, std::memory_order_release);
  recording_.store(false, std::memory_order_release);
  finalizing_.store(false, std::memory_order_release);
  activeWriter_.store(nullptr, std::memory_order_release);
  while (activeWrites_.load(std::memory_order_acquire) != 0)
    juce::Thread::sleep(1);

  // ThreadedWriter's destructor drains its FIFO and finalizes the WAV header.
  writer_.reset();
  writerThread_.stopThread(-1);
}

bool RealtimeMixPrinter::push(
    const juce::AudioBuffer<float> &audio,
    double blockPresentationTimeMs) noexcept {
  if (!recording_.load(std::memory_order_acquire))
    return true;

  const auto samples = audio.getNumSamples();
  if (audio.getNumChannels() < 2 || samples <= 0 || sampleRate_ <= 0.0 ||
      !std::isfinite(blockPresentationTimeMs))
    return true;

  const auto startTime = startTimeMs_.load(std::memory_order_acquire);
  const auto endTime = endTimeMs_.load(std::memory_order_acquire);
  const auto blockEndTimeMs =
      blockPresentationTimeMs + 1000.0 * samples / sampleRate_;
  if (blockEndTimeMs <= startTime)
    return true;

  const auto sampleOffsetFor = [&](double timeMs) {
    return std::clamp(
        static_cast<int>(std::ceil(
            (timeMs - blockPresentationTimeMs) * sampleRate_ / 1000.0)),
        0, samples);
  };
  const int firstSample = blockPresentationTimeMs < startTime
                              ? sampleOffsetFor(startTime)
                              : 0;
  const int endSample = endTime < blockEndTimeMs
                            ? sampleOffsetFor(endTime)
                            : samples;
  const int samplesToWrite = std::max(0, endSample - firstSample);

  if (samplesToWrite == 0) {
    if (blockEndTimeMs >= endTime) {
      recording_.store(false, std::memory_order_release);
      finalizing_.store(true, std::memory_order_release);
    }
    return true;
  }

  activeWrites_.fetch_add(1, std::memory_order_acq_rel);
  auto *writer = activeWriter_.load(std::memory_order_acquire);
  if (!writer) {
    activeWrites_.fetch_sub(1, std::memory_order_release);
    return true;
  }

  const float *channels[]{audio.getReadPointer(0, firstSample),
                          audio.getReadPointer(1, firstSample)};
  const bool accepted = writer->write(channels, samplesToWrite);
  if (accepted) {
    acceptedSamples_.fetch_add(static_cast<uint64_t>(samplesToWrite),
                               std::memory_order_relaxed);
  } else {
    droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
    droppedSamples_.fetch_add(static_cast<uint64_t>(samplesToWrite),
                              std::memory_order_relaxed);
  }
  activeWrites_.fetch_sub(1, std::memory_order_release);
  if (blockEndTimeMs >= endTime) {
    recording_.store(false, std::memory_order_release);
    finalizing_.store(true, std::memory_order_release);
  }
  return accepted;
}

juce::var RealtimeMixPrinter::state() const {
  auto *result = new juce::DynamicObject();
  const auto samples = acceptedSamples_.load(std::memory_order_relaxed);
  result->setProperty("armed", armed_.load(std::memory_order_acquire));
  result->setProperty("recording",
                      recording_.load(std::memory_order_acquire));
  result->setProperty("finalizing",
                      finalizing_.load(std::memory_order_acquire));
  result->setProperty("filePath", file_.getFullPathName());
  result->setProperty("fileName", file_.getFileName());
  result->setProperty("sampleRate", sampleRate_);
  result->setProperty("samples", static_cast<juce::int64>(samples));
  result->setProperty("durationSeconds",
                      sampleRate_ > 0.0 ? static_cast<double>(samples) /
                                              sampleRate_
                                        : 0.0);
  result->setProperty(
      "droppedBlocks",
      static_cast<juce::int64>(droppedBlocks_.load(std::memory_order_relaxed)));
  result->setProperty(
      "droppedSamples",
      static_cast<juce::int64>(droppedSamples_.load(std::memory_order_relaxed)));
  result->setProperty("error", lastError_);
  result->setProperty("format", "Stereo WAV · 32-bit float");
  return juce::var(result);
}

} // namespace fiddle
