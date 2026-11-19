#pragma once

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace fiddle {

/// Dorico's transport, as Fiddle's hosted plug-ins see it (docs/host-transport.md).
///
/// FiddleNative reports where Dorico started and stopped playback. The server
/// pins each start to the presentation time its notes are scheduled at, so a
/// block rendered for presentation time t sits at score time
/// startSeconds + (t - startMs) / 1000 until the stop's presentation time.
/// Plug-ins that follow the timeline (Vienna Performer captures against it)
/// then see the same position as the notes they receive.
///
/// Presentation times lie up to the playback delay ahead, so a quick stop and
/// restart publishes a run while blocks of the one before it are still to be
/// rendered. The transport keeps its last few runs and gives each block the
/// newest run that has begun by then.
///
/// There is one transport, because there is one Dorico. Writers are the
/// server's MIDI thread; readers are render threads and never block.
class HostTransport {
public:
  struct Position {
    bool playing = false;
    double seconds = 0.0;
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
  };
  static constexpr int kRuns = 4;

  /// Playback starts at @p scoreSeconds; its first notes play at
  /// @p presentationMs. A run still going stops there.
  void start(double presentationMs, double scoreSeconds) noexcept;
  /// Playback stops at @p presentationMs. Blocks before it still play.
  void stop(double presentationMs) noexcept;
  /// Dorico reports only the project's initial tempo; see the doc.
  void setTempo(double bpm, int numerator, int denominator) noexcept;
  /// Forget every run (a test starts afresh).
  void reset() noexcept;

  /// Where the block presented at @p presentationMs, @p blockMs long, sits on
  /// the score. A block that overlaps a run plays, so the run's first notes
  /// never land in a stopped block; the one before them starts just short of
  /// the run's start.
  [[nodiscard]] Position at(double presentationMs, double blockMs = 0.0) const noexcept;

private:
  struct Run {
    bool valid = false, stopped = false;
    double startMs = 0.0, startSeconds = 0.0, stopMs = 0.0; // stopMs valid when stopped
  };
  struct Fields {
    Run runs[kRuns]; // newest first
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
  };
  template <typename Edit> void write(Edit &&edit) noexcept;

  // A two-copy latch (seqcount latch): while a writer updates one copy,
  // readers take the other, so a writer descheduled mid-update never stalls
  // a render thread. A reader retries only when a write finishes a copy
  // during its read.
  struct Copy {
    struct AtomicRun {
      std::atomic<bool> valid{false}, stopped{false};
      std::atomic<double> startMs{0.0}, startSeconds{0.0}, stopMs{0.0};
    };
    AtomicRun runs[kRuns];
    std::atomic<double> bpm{120.0};
    std::atomic<int> numerator{4}, denominator{4};
    void store(const Fields &fields) noexcept;
    [[nodiscard]] Fields load() const noexcept;
  };
  juce::SpinLock writer_; // writers only
  Fields current_;        // the writers' copy, under writer_
  std::atomic<uint32_t> sequence_{0};
  Copy copies_[2];
};

/// The process-wide transport FiddleServer drives from Dorico's events.
HostTransport &hostTransport() noexcept;

/// The playhead every hosted plug-in gets. A render thread names the
/// presentation time of the block it is about to process with a BlockScope;
/// plug-ins query the playhead from inside processBlock on that thread.
class TransportPlayHead final : public juce::AudioPlayHead {
public:
  explicit TransportPlayHead(const HostTransport &transport) noexcept
      : transport_(transport) {}

  juce::Optional<PositionInfo> getPosition() const override;

  /// The block this thread renders until the scope ends. Scopes nest: a
  /// strip's latency-advanced time overrides its mixer's block time.
  class BlockScope {
  public:
    BlockScope(double presentationMs, double sampleRate, int numSamples) noexcept;
    /// The block this thread already renders, @p samples later on the score
    /// (negative: earlier). An insert after latency-producing inserts gets
    /// input from earlier on the score than the rack's input.
    struct Shift { int samples; };
    explicit BlockScope(Shift shift) noexcept;
    ~BlockScope();
    BlockScope(const BlockScope &) = delete;
    BlockScope &operator=(const BlockScope &) = delete;

  private:
    double previousMs_, previousRate_, previousLengthMs_;
  };

  /// What a plug-in sees for @p position at @p sampleRate.
  static PositionInfo positionFor(const HostTransport::Position &position,
                                  double sampleRate);

private:
  const HostTransport &transport_;
};

/// The playhead over hostTransport() that HostedPluginSlot installs.
juce::AudioPlayHead &hostPlayHead() noexcept;

} // namespace fiddle
