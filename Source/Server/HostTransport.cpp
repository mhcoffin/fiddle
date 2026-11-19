#include "HostTransport.h"

#include <cmath>
#include <limits>

namespace fiddle {

void HostTransport::Copy::store(const Fields &f) noexcept {
  for (int i = 0; i < kRuns; ++i) {
    runs[i].valid.store(f.runs[i].valid, std::memory_order_relaxed);
    runs[i].stopped.store(f.runs[i].stopped, std::memory_order_relaxed);
    runs[i].startMs.store(f.runs[i].startMs, std::memory_order_relaxed);
    runs[i].startSeconds.store(f.runs[i].startSeconds, std::memory_order_relaxed);
    runs[i].stopMs.store(f.runs[i].stopMs, std::memory_order_relaxed);
    runs[i].startFrame.store(f.runs[i].startFrame, std::memory_order_relaxed);
    runs[i].stopFrame.store(f.runs[i].stopFrame, std::memory_order_relaxed);
    runs[i].stream.store(f.runs[i].stream, std::memory_order_relaxed);
  }
  bpm.store(f.bpm, std::memory_order_relaxed);
  numerator.store(f.numerator, std::memory_order_relaxed);
  denominator.store(f.denominator, std::memory_order_relaxed);
}

HostTransport::Fields HostTransport::Copy::load() const noexcept {
  Fields f;
  for (int i = 0; i < kRuns; ++i) {
    f.runs[i].valid = runs[i].valid.load(std::memory_order_relaxed);
    f.runs[i].stopped = runs[i].stopped.load(std::memory_order_relaxed);
    f.runs[i].startMs = runs[i].startMs.load(std::memory_order_relaxed);
    f.runs[i].startSeconds = runs[i].startSeconds.load(std::memory_order_relaxed);
    f.runs[i].stopMs = runs[i].stopMs.load(std::memory_order_relaxed);
    f.runs[i].startFrame = runs[i].startFrame.load(std::memory_order_relaxed);
    f.runs[i].stopFrame = runs[i].stopFrame.load(std::memory_order_relaxed);
    f.runs[i].stream = runs[i].stream.load(std::memory_order_relaxed);
  }
  f.bpm = bpm.load(std::memory_order_relaxed);
  f.numerator = numerator.load(std::memory_order_relaxed);
  f.denominator = denominator.load(std::memory_order_relaxed);
  return f;
}

template <typename Edit> void HostTransport::write(Edit &&edit) noexcept {
  const juce::SpinLock::ScopedLockType lock(writer_);
  edit(current_);
  // Readers follow the sequence's low bit to the copy not being written.
  sequence_.fetch_add(1, std::memory_order_release); // readers -> copy 1
  std::atomic_thread_fence(std::memory_order_release);
  copies_[0].store(current_);
  sequence_.fetch_add(1, std::memory_order_release); // readers -> copy 0
  std::atomic_thread_fence(std::memory_order_release);
  copies_[1].store(current_);
}

namespace {
template <typename Run>
void stopRun(Run &run, double presentationMs, StreamFrame frame) noexcept {
  run.stopped = true;
  run.stopMs = std::max(presentationMs, run.startMs);
  // A run counts frames only if both ends have them, in the same stream.
  run.stopFrame = run.startFrame >= 0 && frame.known() && frame.stream == run.stream
                      ? std::max(frame.frame, run.startFrame)
                      : HostTransport::kNoFrame;
}
} // namespace

void HostTransport::start(double presentationMs, double scoreSeconds,
                          StreamFrame frame) noexcept {
  write([&](Fields &f) {
    auto &previous = f.runs[0];
    if (previous.valid && !previous.stopped)
      stopRun(previous, presentationMs, frame);
    for (int i = kRuns - 1; i > 0; --i) f.runs[i] = f.runs[i - 1];
    f.runs[0] = Run{true, false, presentationMs, std::max(0.0, scoreSeconds), 0.0,
                    frame.known() ? frame.frame : kNoFrame, kNoFrame, frame.stream};
  });
}

void HostTransport::stop(double presentationMs, StreamFrame frame) noexcept {
  write([&](Fields &f) {
    auto &run = f.runs[0];
    if (!run.valid || run.stopped) return;
    stopRun(run, presentationMs, frame);
  });
}

void HostTransport::setTempo(double bpm, int numerator, int denominator) noexcept {
  if (!(bpm > 0.0)) return;
  write([&](Fields &f) {
    f.bpm = bpm;
    if (numerator > 0 && denominator > 0) {
      f.numerator = numerator;
      f.denominator = denominator;
    }
  });
}

void HostTransport::reset() noexcept {
  write([](Fields &f) {
    for (auto &run : f.runs) run = {};
  });
}

HostTransport::Position HostTransport::at(const Block &block) const noexcept {
  Fields f;
  for (;;) {
    const auto before = sequence_.load(std::memory_order_acquire);
    f = copies_[before & 1u].load();
    std::atomic_thread_fence(std::memory_order_acquire);
    if (sequence_.load(std::memory_order_relaxed) == before) break;
  }
  Position position;
  position.bpm = f.bpm;
  position.numerator = f.numerator;
  position.denominator = f.denominator;
  const double presentationMs = block.presentationMs;
  const double blockMs = std::max(0.0, block.lengthMs);
  // Frames when the run and the block have them in the same stream, else
  // presentation time. After a device restart the run's frames belong to the
  // old stream, and the new stream's count from its own start.
  const int64_t frame = block.frame.frame;
  const auto counted = [&](const Run &run) {
    return block.frame.known() && run.startFrame >= 0 && block.frame.stream == run.stream &&
           block.sampleRate > 0.0 && (!run.stopped || run.stopFrame >= 0);
  };
  const auto reached = [&](const Run &run) {
    if (counted(run))
      return frame + std::max(1, block.frames) > run.startFrame;
    return presentationMs + blockMs > run.startMs || presentationMs >= run.startMs;
  };
  // The newest run this block reaches; before them all, the oldest kept run.
  const Run *run = nullptr;
  for (const auto &candidate : f.runs) {
    if (!candidate.valid) break;
    run = &candidate;
    if (reached(candidate)) break;
  }
  if (run == nullptr) return position;
  // Before its first notes, a run waits at its start; after its stop, it
  // rests where it stopped.
  const bool begun = reached(*run);
  if (counted(*run)) {
    const bool beforeStop = !run->stopped || frame < run->stopFrame;
    position.playing = begun && beforeStop;
    int64_t offset = 0;
    if (position.playing)
      offset = frame - run->startFrame;
    else if (begun)
      offset = run->stopFrame - run->startFrame;
    const auto startSample = static_cast<int64_t>(std::llround(run->startSeconds * block.sampleRate));
    position.samples = std::max<int64_t>(0, startSample + offset);
    position.seconds = double(*position.samples) / block.sampleRate;
    return position;
  }
  const auto end = run->stopped ? run->stopMs : std::numeric_limits<double>::infinity();
  position.playing = begun && presentationMs < end;
  double offsetMs = 0.0;
  if (position.playing)
    offsetMs = presentationMs - run->startMs;
  else if (begun)
    offsetMs = end - run->startMs;
  position.seconds = std::max(0.0, run->startSeconds + offsetMs / 1000.0);
  return position;
}

HostTransport &hostTransport() noexcept {
  static HostTransport transport;
  return transport;
}

namespace {
constexpr double kNoBlock = std::numeric_limits<double>::quiet_NaN();
// The block this thread renders; presentationMs is NaN outside a scope.
thread_local HostTransport::Block currentBlock{kNoBlock, 0.0, {}, 0, 0.0};
// For a plug-in that asks outside processBlock (from its editor, say).
std::atomic<double> lastBlockMs{kNoBlock};
std::atomic<double> lastBlockRate{0.0};
std::atomic<double> lastBlockLengthMs{0.0};
} // namespace

TransportPlayHead::BlockScope::BlockScope(double presentationMs, double sampleRate,
                                          int numSamples, StreamFrame frame) noexcept
    : previous_(currentBlock) {
  currentBlock.presentationMs = presentationMs;
  currentBlock.sampleRate = sampleRate;
  currentBlock.frames = numSamples;
  currentBlock.lengthMs = sampleRate > 0.0 ? 1000.0 * numSamples / sampleRate : 0.0;
  currentBlock.frame = frame;
  lastBlockMs.store(presentationMs, std::memory_order_relaxed);
  lastBlockRate.store(sampleRate, std::memory_order_relaxed);
  lastBlockLengthMs.store(currentBlock.lengthMs, std::memory_order_relaxed);
}

TransportPlayHead::BlockScope::BlockScope(Shift shift) noexcept : previous_(currentBlock) {
  if (std::isnan(currentBlock.presentationMs) || currentBlock.sampleRate <= 0.0) return;
  currentBlock.presentationMs += 1000.0 * shift.samples / currentBlock.sampleRate;
  currentBlock.frame = currentBlock.frame.ahead(shift.samples);
}

TransportPlayHead::BlockScope::~BlockScope() { currentBlock = previous_; }

juce::AudioPlayHead::PositionInfo
TransportPlayHead::positionFor(const HostTransport::Position &position, double sampleRate) {
  PositionInfo info;
  info.setIsPlaying(position.playing);
  info.setIsRecording(false);
  info.setIsLooping(false);
  info.setTimeInSeconds(position.seconds);
  if (position.samples)
    info.setTimeInSamples(static_cast<juce::int64>(*position.samples));
  else if (sampleRate > 0.0)
    info.setTimeInSamples(static_cast<juce::int64>(std::llround(position.seconds * sampleRate)));
  info.setBpm(position.bpm);
  info.setTimeSignature(TimeSignature{position.numerator, position.denominator});
  // Dorico's own beat clock runs at its initial tempo too (FiddleProcessor.cpp).
  const double ppq = position.seconds * position.bpm / 60.0;
  info.setPpqPosition(ppq);
  const double barQuarters = 4.0 * position.numerator / position.denominator;
  if (barQuarters > 0.0) {
    const double bars = std::floor(ppq / barQuarters + 1e-9);
    info.setBarCount(static_cast<juce::int64>(bars));
    info.setPpqPositionOfLastBarStart(bars * barQuarters);
  }
  return info;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> TransportPlayHead::getPosition() const {
  auto block = currentBlock;
  const bool scoped = !std::isnan(block.presentationMs);
  if (!scoped) {
    block.presentationMs = lastBlockMs.load(std::memory_order_relaxed);
    block.sampleRate = lastBlockRate.load(std::memory_order_relaxed);
    block.lengthMs = lastBlockLengthMs.load(std::memory_order_relaxed);
    // Presentation time only: a frame and its stream read separately could
    // come from different blocks, and an editor needs no exact sample.
    block.frame = {};
  }
  const double ms = block.presentationMs, rate = block.sampleRate;
  if (std::isnan(ms)) return positionFor(HostTransport::Position{}, rate);
  return positionFor(transport_.at(block), rate);
}

juce::AudioPlayHead &hostPlayHead() noexcept {
  static TransportPlayHead playHead(hostTransport());
  return playHead;
}

} // namespace fiddle
