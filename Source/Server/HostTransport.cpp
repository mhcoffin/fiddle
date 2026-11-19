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

void HostTransport::start(double presentationMs, double scoreSeconds) noexcept {
  write([&](Fields &f) {
    auto &previous = f.runs[0];
    if (previous.valid && !previous.stopped) {
      previous.stopped = true;
      previous.stopMs = std::max(presentationMs, previous.startMs);
    }
    for (int i = kRuns - 1; i > 0; --i) f.runs[i] = f.runs[i - 1];
    f.runs[0] = Run{true, false, presentationMs, std::max(0.0, scoreSeconds), 0.0};
  });
}

void HostTransport::stop(double presentationMs) noexcept {
  write([&](Fields &f) {
    auto &run = f.runs[0];
    if (!run.valid || run.stopped) return;
    run.stopped = true;
    run.stopMs = std::max(presentationMs, run.startMs);
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

HostTransport::Position HostTransport::at(double presentationMs,
                                          double blockMs) const noexcept {
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
  // The newest run this block reaches; before them all, the oldest kept run.
  const Run *run = nullptr;
  for (const auto &candidate : f.runs) {
    if (!candidate.valid) break;
    run = &candidate;
    if (presentationMs + std::max(0.0, blockMs) > candidate.startMs ||
        presentationMs >= candidate.startMs)
      break;
  }
  if (run == nullptr) return position;
  // Before its first notes, a run waits at its start; after its stop, it
  // rests where it stopped.
  const bool begun = presentationMs + std::max(0.0, blockMs) > run->startMs ||
                     presentationMs >= run->startMs;
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
thread_local double blockMs = kNoBlock;
thread_local double blockRate = 0.0;
thread_local double blockLengthMs = 0.0;
// For a plug-in that asks outside processBlock (from its editor, say).
std::atomic<double> lastBlockMs{kNoBlock};
std::atomic<double> lastBlockRate{0.0};
std::atomic<double> lastBlockLengthMs{0.0};
} // namespace

TransportPlayHead::BlockScope::BlockScope(double presentationMs, double sampleRate,
                                          int numSamples) noexcept
    : previousMs_(blockMs), previousRate_(blockRate), previousLengthMs_(blockLengthMs) {
  blockMs = presentationMs;
  blockRate = sampleRate;
  blockLengthMs = sampleRate > 0.0 ? 1000.0 * numSamples / sampleRate : 0.0;
  lastBlockMs.store(presentationMs, std::memory_order_relaxed);
  lastBlockRate.store(sampleRate, std::memory_order_relaxed);
  lastBlockLengthMs.store(blockLengthMs, std::memory_order_relaxed);
}

TransportPlayHead::BlockScope::BlockScope(Shift shift) noexcept
    : previousMs_(blockMs), previousRate_(blockRate), previousLengthMs_(blockLengthMs) {
  if (std::isnan(blockMs) || blockRate <= 0.0) return;
  blockMs += 1000.0 * shift.samples / blockRate;
}

TransportPlayHead::BlockScope::~BlockScope() {
  blockMs = previousMs_;
  blockRate = previousRate_;
  blockLengthMs = previousLengthMs_;
}

juce::AudioPlayHead::PositionInfo
TransportPlayHead::positionFor(const HostTransport::Position &position, double sampleRate) {
  PositionInfo info;
  info.setIsPlaying(position.playing);
  info.setIsRecording(false);
  info.setIsLooping(false);
  info.setTimeInSeconds(position.seconds);
  if (sampleRate > 0.0)
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
  double ms = blockMs, rate = blockRate, length = blockLengthMs;
  if (std::isnan(ms)) {
    ms = lastBlockMs.load(std::memory_order_relaxed);
    rate = lastBlockRate.load(std::memory_order_relaxed);
    length = lastBlockLengthMs.load(std::memory_order_relaxed);
  }
  if (std::isnan(ms)) return positionFor(HostTransport::Position{}, rate);
  return positionFor(transport_.at(ms, length), rate);
}

juce::AudioPlayHead &hostPlayHead() noexcept {
  static TransportPlayHead playHead(hostTransport());
  return playHead;
}

} // namespace fiddle
