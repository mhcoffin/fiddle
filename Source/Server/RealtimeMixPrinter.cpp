#include "RealtimeMixPrinter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fiddle {
namespace {
// JUCE's threaded writer does not expose disk errors. Preserve them through
// writes, header seeks and the final buffered-stream flush as well as FIFO loss.
class CheckedPrintStream final : public juce::FileOutputStream {
public:
  CheckedPrintStream(const juce::File &file, std::atomic<int> &failure)
      : FileOutputStream(file), failure_(failure) {}
  ~CheckedPrintStream() override { flush(); }
  bool write(const void *data, size_t size) override {
    const bool ok = FileOutputStream::write(data, size);
    if (!ok || getStatus().failed()) failure_.store(1);
    return ok;
  }
  bool setPosition(juce::int64 position) override {
    const bool ok = FileOutputStream::setPosition(position);
    if (!ok || getStatus().failed()) failure_.store(1);
    return ok;
  }
  void flush() override {
    FileOutputStream::flush();
    if (getStatus().failed()) failure_.store(1);
  }
private:
  std::atomic<int> &failure_;
};

// One multichannel FIFO feeds every file. Admission is all-or-nothing, so a
// slow disk cannot drop different blocks from different stems.
class StemWriter final : public juce::AudioFormatWriter {
public:
  StemWriter(std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers,
             double rate, std::atomic<int> &failure)
      : AudioFormatWriter(nullptr, "Synchronized stereo WAVs", rate,
                          static_cast<unsigned>(writers.size() * 2), 32),
        writers_(std::move(writers)), failure_(failure) {
    usesFloatingPointData = true;
  }
  ~StemWriter() override {
    // Flush only on finalization, never periodically during playback.
    for (auto &writer : writers_)
      if (!writer->flush()) failure_.store(1);
  }
  bool write(const int **data, int samples) override {
    if (diskFailed_) return false;
    for (size_t i = 0; i < writers_.size(); ++i) {
      const int *stereo[]{data[i * 2], data[i * 2 + 1], nullptr};
      if (!writers_[i]->write(stereo, samples)) {
        diskFailed_ = true;
        failure_.store(1);
        return false;
      }
    }
    return true;
  }
private:
  std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers_;
  std::atomic<int> &failure_;
  bool diskFailed_ = false;
};
}

RealtimeMixPrinter::RealtimeMixPrinter() = default;

RealtimeMixPrinter::~RealtimeMixPrinter() { stop(); }

juce::String RealtimeMixPrinter::start(const juce::File &file,
                                       double sampleRate,
                                       int maximumBlockSize, double tailSeconds) {
  stop();
  includeMaster_ = true;
  capture_.buses.clear();
  file_ = file;
  return open({file}, sampleRate, maximumBlockSize, true, tailSeconds);
}

juce::String RealtimeMixPrinter::startStems(
    const juce::File &parentFolder, double sampleRate, int maximumBlockSize,
    const std::vector<Stem> &buses, bool includeMaster, int masterLatencySamples,
    double tailSeconds) {
  if (isArmed()) return "Finish the current print before arming another";
  const auto count = buses.size() + (includeMaster ? 1 : 0);
  if (count == 0 || count > 64 || maximumBlockSize <= 0 || maximumBlockSize > 4096 ||
      !std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000 ||
      masterLatencySamples < 0 || masterLatencySamples > sampleRate * 30 ||
      !std::isfinite(tailSeconds) || tailSeconds < 0 || tailSeconds > 60 ||
      !parentFolder.isDirectory())
    return "Choose 1–64 outputs and a valid destination/audio format";
  if (static_cast<uint64_t>(masterLatencySamples) * buses.size() * 2 * sizeof(float) > 64 * 1024 * 1024)
    return "Master latency requires too much stem alignment memory; reduce latency or select fewer buses";
  juce::StringArray ids;
  for (const auto &bus : buses) {
    if (bus.id.isEmpty() || ids.contains(bus.id)) return "Invalid or duplicate bus selection";
    ids.add(bus.id);
  }
  stop();
  includeMaster_ = includeMaster;
  capture_.buses.clear();
  capture_.masterLatency = masterLatencySamples;
  file_ = parentFolder.getChildFile("Fiddle Stems " +
      juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + " " +
      juce::Uuid().toString().substring(0, 8));
  if (file_.exists() || file_.createDirectory().failed())
    return "Could not create a new stem take folder";
  std::vector<juce::File> files;
  if (includeMaster) files.push_back(file_.getChildFile("00 Master.wav"));
  int index = 1;
  for (const auto &bus : buses) {
    auto name = juce::File::createLegalFileName(bus.name).substring(0, 80).trim();
    if (name.isEmpty()) name = "Bus";
    files.push_back(file_.getChildFile(juce::String(index++).paddedLeft('0', 2) + " " + name + ".wav"));
    capture_.buses.push_back(std::make_unique<BusPrintCapture::Bus>(bus.id, maximumBlockSize, masterLatencySamples));
  }
  return open(files, sampleRate, maximumBlockSize, false, tailSeconds);
}

juce::String RealtimeMixPrinter::open(const std::vector<juce::File> &files,
                                     double sampleRate, int maximumBlockSize,
                                     bool replace, double tailSeconds) {
  lastError_.clear();
  files_ = files;
  sampleRate_ = sampleRate;
  tailSeconds_ = tailSeconds;
  failure_.store(0);
  expectedNextFrame_ = 0;
  acceptedSamples_.store(0, std::memory_order_relaxed);
  droppedBlocks_.store(0, std::memory_order_relaxed);
  droppedSamples_.store(0, std::memory_order_relaxed);

  const auto fail = [this](const juce::String &message) {
    lastError_ = message;
    sampleRate_ = 0.0;
    return message;
  };

  if (files.empty() || !std::isfinite(sampleRate) ||
      sampleRate < 8000.0 || sampleRate > 384000.0 || maximumBlockSize <= 0 || maximumBlockSize > 4096 ||
      !std::isfinite(tailSeconds) || tailSeconds < 0 || tailSeconds > 60)
    return fail("Invalid print file or audio format");

  std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;
  for (const auto &file : files) {
    if (file == juce::File{}) return fail("Invalid print file");
    if (file.getParentDirectory().createDirectory().failed())
      return fail("Could not create the print destination folder");
    if (!replace && file.exists())
      return fail("Stem file already exists; no files were replaced");
    if (file.existsAsFile() && !file.deleteFile())
      return fail("Could not replace the selected WAV file");

    auto fileStream = std::make_unique<CheckedPrintStream>(file, failure_);
    if (fileStream->failedToOpen())
      return fail("Could not open the selected WAV file for writing");
    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withChannelLayout(juce::AudioChannelSet::stereo())
                             .withBitsPerSample(32)
                             .withSampleFormat(
                                 juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto formatWriter = wav.createWriterFor(stream, options);
    if (!formatWriter)
      return fail("Could not create a 32-bit floating-point WAV writer");
    if (failure_.load() != 0)
      return fail("Could not write the WAV header");
    writers.push_back(std::move(formatWriter));
  }
  auto formatWriter = std::make_unique<StemWriter>(std::move(writers), sampleRate, failure_);
  combined_.setSize(static_cast<int>(files.size() * 2), maximumBlockSize);
  writePointers_.resize(files.size() * 2);

  // Instrument streaming and the render-ahead worker must always win over
  // export I/O. The FIFO absorbs short low-priority scheduling delays; if the
  // disk still cannot keep up, write() reports a dropped block rather than
  // allowing the print thread to compete with Dorico's return path.
  if (!writerThread_.startThread(juce::Thread::Priority::low))
    return fail("Could not start the background mix-print writer");

  const auto eightSeconds = static_cast<int>(std::min(
      sampleRate * 8.0, static_cast<double>(std::numeric_limits<int>::max())));
  // Bound aggregate FIFO storage to 64 MiB even with many high-rate stems.
  const auto memoryLimitSamples = static_cast<int>((64 * 1024 * 1024) / (files.size() * 2 * sizeof(float)));
  const auto fifoSamples = std::max(maximumBlockSize * 8, std::min(eightSeconds, memoryLimitSamples));
  writer_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
      formatWriter.release(), writerThread_, fifoSamples);
  // Do not periodically seek back and rewrite the WAV header during playback.
  // ThreadedWriter finalizes it when stop() drains and destroys the writer.
  activeWriter_.store(writer_.get(), std::memory_order_seq_cst);
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
      recording_.load(std::memory_order_acquire) || failure_.load() != 0 ||
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
  // Only the first Stop sets the boundary; duplicate notifications must not
  // extend the tail indefinitely.
  double expected = std::numeric_limits<double>::infinity();
  return endTimeMs_.compare_exchange_strong(expected,
      presentationTimeMs + 1000.0 * tailSeconds_, std::memory_order_acq_rel);
}

bool RealtimeMixPrinter::needsFinalizing(
    double currentPresentationTimeMs) const noexcept {
  if (armed_.load() && failure_.load() != 0) return true;
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
  activeWriter_.store(nullptr, std::memory_order_seq_cst);
  while (activeWrites_.load(std::memory_order_seq_cst) != 0)
    juce::Thread::sleep(1);

  // ThreadedWriter's destructor drains its FIFO and finalizes the WAV header.
  writer_.reset();
  writerThread_.stopThread(-1);
  // An in-flight render may have reached its end boundary while we waited.
  finalizing_.store(false, std::memory_order_release);
}

bool RealtimeMixPrinter::push(
    const juce::AudioBuffer<float> &audio,
    double blockPresentationTimeMs, uint64_t startFrame, int sampleCount) noexcept {
  if (!recording_.load(std::memory_order_acquire) || failure_.load() != 0)
    return true;

  const auto samples = sampleCount < 0 ? audio.getNumSamples() : sampleCount;
  if (audio.getNumChannels() != static_cast<int>(files_.size() * 2) || samples <= 0 || samples > audio.getNumSamples() || sampleRate_ <= 0.0 ||
      !std::isfinite(blockPresentationTimeMs))
    return true;

  const auto startTime = startTimeMs_.load(std::memory_order_acquire);
  const auto endTime = endTimeMs_.load(std::memory_order_acquire);
  const auto blockEndTimeMs =
      blockPresentationTimeMs + 1000.0 * samples / sampleRate_;
  // Host callback timestamps are refreshed on each pull and carry scheduling
  // jitter. They are not a sample-continuity clock. Check the actual stream
  // cursor, and only after capturing samples: idle/pre-roll gaps are not lost
  // take audio. Keep checking even if a clock correction moves time backwards.
  if (acceptedSamples_.load(std::memory_order_relaxed) != 0 &&
      startFrame != expectedNextFrame_) {
    failRealtime(5);
    return false;
  }
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

  activeWrites_.fetch_add(1, std::memory_order_seq_cst);
  auto *writer = activeWriter_.load(std::memory_order_seq_cst);
  if (!writer) {
    activeWrites_.fetch_sub(1, std::memory_order_seq_cst);
    return true;
  }

  for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    writePointers_[static_cast<size_t>(ch)] = audio.getReadPointer(ch, firstSample);
  const bool accepted = writer->write(writePointers_.data(), samplesToWrite);
  if (accepted) {
    expectedNextFrame_ = startFrame + static_cast<uint64_t>(samples);
    acceptedSamples_.fetch_add(static_cast<uint64_t>(samplesToWrite),
                               std::memory_order_relaxed);
  } else {
    droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
    droppedSamples_.fetch_add(static_cast<uint64_t>(samplesToWrite),
                              std::memory_order_relaxed);
    failRealtime(4);
  }
  activeWrites_.fetch_sub(1, std::memory_order_seq_cst);
  if (blockEndTimeMs >= endTime) {
    recording_.store(false, std::memory_order_release);
    finalizing_.store(true, std::memory_order_release);
  }
  return accepted;
}

void RealtimeMixPrinter::failRealtime(int reason) noexcept {
  int expected = 0;
  failure_.compare_exchange_strong(expected, reason);
  recording_.store(false);
  finalizing_.store(true);
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
  result->setProperty("tailSeconds", tailSeconds_);
  result->setProperty("capturingTail", tailSeconds_ > 0 && isRecording() &&
      std::isfinite(endTimeMs_.load(std::memory_order_acquire)));
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
  juce::String error = lastError_;
  switch (failure_.load()) {
  case 1: error = "Disk write or WAV finalization failed; the entire take is incomplete."; break;
  case 2: error = "Master latency changed during printing; the entire take is incomplete. Rearm and replay."; break;
  case 3: error = "A selected bus was removed during printing; the entire take is incomplete."; break;
  case 4: error = "The disk queue filled; printing stopped and the entire take is incomplete."; break;
  case 5: error = "The render timeline skipped audio; the entire take is incomplete. Rearm and replay."; break;
  default: break;
  }
  result->setProperty("error", error);
  result->setProperty("incomplete", error.isNotEmpty());
  juce::Array<juce::var> outputs;
  for (const auto &file : files_) outputs.add(file.getFullPathName());
  result->setProperty("files", outputs);
  juce::Array<juce::var> busIds;
  for (const auto &bus : capture_.buses) busIds.add(bus->id);
  result->setProperty("busIds", busIds);
  result->setProperty("includeMaster", includeMaster_);
  result->setProperty("stemMode", file_.isDirectory());
  result->setProperty("format", juce::String::fromUTF8("Stereo WAV · 32-bit float"));
  return juce::var(result);
}

} // namespace fiddle
