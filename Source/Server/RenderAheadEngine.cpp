#include "RenderAheadEngine.h"
#include "MixerModel.h"
#include <new>

namespace fiddle {
namespace {
constexpr uint32_t kPerformanceGraceMs = 1000;
constexpr uint32_t kMaximumTailMs = 5000;
constexpr double kQuietSeconds = 0.25;
constexpr float kQuietMagnitude = 1.0e-5f;
}

RenderAheadEngine::RenderAheadEngine(MixerModel &mixer, AudioRenderDiagnostics &recorder,
                                     juce::File file)
    : Thread("Fiddle render ahead"), mixer_(mixer), recorder_(recorder), file_(std::move(file)) {}
RenderAheadEngine::~RenderAheadEngine() { stop(); }

void RenderAheadEngine::stop() {
  idle_.store(true, std::memory_order_release);
  if (auto s = stream_.read(); s.get()) s.get()->ring->active.store(0, std::memory_order_release);
  signalThreadShouldExit();
  notify();
  // Never force-terminate a thread inside vendor code or free its processors.
  waitForThreadToExit(-1);
  mixPrinter_.stop();
  recorder_.publish();
}

juce::String RenderAheadEngine::startMixPrint(const juce::File &file, double tailSeconds) {
  if (mixPrinter_.isArmed()) return "Finish the current print before arming another";
  auto stream = stream_.read();
  if (!stream.get() || !stream.get()->ring || !isThreadRunning())
    return "Audio rendering is not running";
  return mixPrinter_.start(file, stream.get()->ring->sampleRate,
                           static_cast<int>(stream.get()->ring->blockSize), tailSeconds);
}

juce::String RenderAheadEngine::startStemPrint(const juce::File &folder,
                                              const juce::StringArray &busIds,
                                              bool includeMaster, double tailSeconds) {
  auto stream = stream_.read();
  if (!stream.get() || !stream.get()->ring || !isThreadRunning())
    return "Audio rendering is not running";
  std::vector<RealtimeMixPrinter::Stem> buses;
  for (const auto &id : busIds) {
    auto *bus = mixer_.getGroupBus(id);
    if (!bus) return "A selected bus no longer exists; select outputs again";
    buses.push_back({id, bus->name});
  }
  return mixPrinter_.startStems(folder, stream.get()->ring->sampleRate,
      static_cast<int>(stream.get()->ring->blockSize), buses, includeMaster,
      mixer_.masterAudio().latencySamples(), tailSeconds);
}

bool RenderAheadEngine::beginMixPrintAt(double presentationTimeMs) noexcept {
  return mixPrinter_.beginAt(presentationTimeMs);
}

bool RenderAheadEngine::endMixPrintAt(double presentationTimeMs) noexcept {
  return mixPrinter_.endAt(presentationTimeMs);
}

void RenderAheadEngine::stopMixPrint() { mixPrinter_.stop(); }

void RenderAheadEngine::setTransportActive(bool active) noexcept {
  transportActive_.store(active, std::memory_order_release);
  if (active)
    idle_.store(false, std::memory_order_release);
  notify();
}

void RenderAheadEngine::notePerformanceActivity() noexcept {
  auto now = juce::Time::getMillisecondCounter();
  if (now == 0)
    now = 1;
  lastPerformanceActivityMs_.store(now, std::memory_order_release);
  idle_.store(false, std::memory_order_release);
  notify();
}

void RenderAheadEngine::setAuditionNotesActive(bool active) noexcept {
  const auto changed =
      auditionNotesActive_.exchange(active, std::memory_order_acq_rel) != active;
  if (active)
    idle_.store(false, std::memory_order_release);
  if (changed)
    notify();
}

bool RenderAheadEngine::mixPrintIsArmed() const noexcept {
  return mixPrinter_.isArmed();
}

bool RenderAheadEngine::mixPrintNeedsFinalizing(
    double currentPresentationTimeMs) const noexcept {
  return mixPrinter_.needsFinalizing(currentPresentationTimeMs);
}

juce::var RenderAheadEngine::mixPrintState() const {
  return mixPrinter_.state();
}

juce::String RenderAheadEngine::start(double rate, int blockSize) {
  stop();
  if (!std::isfinite(rate) || rate < 8000 || rate > 384000 || blockSize <= 0)
    return "Invalid audio sample rate or processing block size";
  const auto reserve = AudioStreamRing::reserveFrames(rate, uint32_t(blockSize));
  if (reserve == 0) return "Render-ahead supports processing blocks up to 4096 frames";
  auto stream = std::make_unique<Stream>();
  if (file_.getParentDirectory().createDirectory().failed()) return "Cannot create the audio cache directory";
  // New inode for every generation: old consumers can finish reading their old
  // mapping without ever sharing reset cursors with a new producer.
  juce::TemporaryFile temporary(file_);
  {
    juce::FileOutputStream output(temporary.getFile());
    if (!output.openedOk() || !output.setPosition(sizeof(AudioStreamRing) - 1) || !output.writeByte(0))
      return "Cannot allocate the audio stream";
    output.flush();
  }
  stream->mapping = std::make_unique<juce::MemoryMappedFile>(temporary.getFile(), juce::MemoryMappedFile::readWrite);
  if (!stream->mapping->getData()) return "Cannot map the audio stream";
  stream->ring = new (stream->mapping->getData()) AudioStreamRing{};
  auto &ring = *stream->ring;
  ring.streamId = static_cast<uint64_t>(audioStreamTimeMs() * 1000.0);
  ring.sampleRate = rate;
  ring.blockSize = uint32_t(blockSize);
  ring.targetFrames = reserve;
  // Prime silence, not musical events. The consumer establishes the stream's
  // presentation clock on its first pull; subsequent DSP uses that clock.
  ring.writeFrame.store(reserve, std::memory_order_relaxed);
  ring.magic.store(AudioStreamRing::magicValue, std::memory_order_release);
  if (!temporary.overwriteTargetFileWithTemporary()) return "Cannot publish the audio stream";
  stream->scratch.setSize(2, blockSize);
  mixer_.prepareToPlay(rate, blockSize);
  recorder_.prepare(rate);
  stream_.publish(std::move(stream));
  stream_.reclaimRetired();
  idle_.store(!performanceRequested(juce::Time::getMillisecondCounter()),
              std::memory_order_release);
  const auto period = 1000.0 * blockSize / rate;
  const auto options = RealtimeOptions{}.withPeriodMs(period)
      .withProcessingTimeMs(period * 0.75).withMaximumProcessingTimeMs(period);
  realtime_.store(startRealtimeThread(options), std::memory_order_relaxed);
  if (!isThreadRunning() && !startThread(Priority::high)) return "Could not start the audio rendering worker";
  if (auto s = stream_.read(); s.get()) s.get()->ring->active.store(1, std::memory_order_release);
  return {};
}

void RenderAheadEngine::run() {
  auto stream = stream_.read();
  if (!stream.get()) return;
  auto &s = *stream.get();
  auto &ring = *s.ring;
  double previousEndMs = 0;
  uint64_t quietFrames = 0;
  while (!threadShouldExit()) {
    const auto now = juce::Time::getMillisecondCounter();
    const bool requested = performanceRequested(now);
    if (requested) {
      idle_.store(false, std::memory_order_release);
      quietFrames = 0;
    }
    const bool idle = idle_.load(std::memory_order_acquire);
    const auto plan = RenderAheadPlan::next(ring);
    if (!ring.active.load(std::memory_order_acquire) || !plan.render) {
      // Consumer never signals/locks here. Only the non-device worker waits.
      wait(idle ? idleWaitMilliseconds(ring) : 1);
      continue;
    }

    const auto start = juce::Time::getMillisecondCounterHiRes();
    if (previousEndMs > 0 &&
        std::abs(plan.presentationTimeMs - previousEndMs) > 100.0)
      s.clockJumps.fetch_add(1, std::memory_order_relaxed);
    PluginRenderDiagnostics::beginBlock();
    s.scratch.clear();

    if (idle) {
      const bool pushed = ring.push(
          plan.startFrame, s.scratch.getReadPointer(0),
          s.scratch.getReadPointer(1), ring.blockSize);
      const auto end = juce::Time::getMillisecondCounterHiRes();
      recorder_.record(start, end, int(ring.blockSize), !pushed, false, 0.0,
                       false, false);
      s.renderedBlocks.fetch_add(1, std::memory_order_relaxed);
      s.idleBlocks.fetch_add(1, std::memory_order_relaxed);
      s.skippedFrames.fetch_add(plan.skippedFrames,
                                std::memory_order_relaxed);
      previousEndMs = plan.presentationTimeMs +
                      1000.0 * ring.blockSize / ring.sampleRate;
      continue;
    }

    AudioProcessingGate::Render render;
    if (!render) {
      const auto waitStart = juce::Time::getMillisecondCounterHiRes();
      wait(1);
      const auto elapsed = juce::Time::getMillisecondCounterHiRes() - waitStart;
      s.controlWaitUs.fetch_add(static_cast<uint64_t>(std::max(0.0, elapsed) * 1000.0),
                               std::memory_order_relaxed);
      s.controlWaitCount.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    mixPrinter_.renderAndPush(s.scratch, plan.presentationTimeMs, plan.startFrame,
        [&](BusPrintCapture *capture) {
          mixer_.processBlock(s.scratch, plan.presentationTimeMs, capture);
        });
    const bool pushed = ring.push(plan.startFrame, s.scratch.getReadPointer(0),
                                   s.scratch.getReadPointer(1), ring.blockSize);
    const auto end = juce::Time::getMillisecondCounterHiRes();
    recorder_.record(start, end, int(ring.blockSize), !pushed, false,
                      PluginRenderDiagnostics::blockWorkMs(), false,
                      mixer_.renderWorkerCount() > 1);
    s.renderedBlocks.fetch_add(1, std::memory_order_relaxed);
    s.skippedFrames.fetch_add(plan.skippedFrames, std::memory_order_relaxed);
    previousEndMs = plan.presentationTimeMs + 1000.0 * ring.blockSize / ring.sampleRate;

    if (!requested) {
      const auto magnitude = juce::jmax(
          s.scratch.getMagnitude(0, 0, int(ring.blockSize)),
          s.scratch.getMagnitude(1, 0, int(ring.blockSize)));
      quietFrames = magnitude <= kQuietMagnitude
                        ? quietFrames + ring.blockSize
                        : 0;
      const auto last =
          lastPerformanceActivityMs_.load(std::memory_order_acquire);
      const auto elapsed =
          last == 0 ? kMaximumTailMs + 1
                    : static_cast<uint32_t>(now - last);
      const auto quietTarget = static_cast<uint64_t>(
          std::ceil(kQuietSeconds * ring.sampleRate));
      if (quietFrames >= quietTarget || elapsed > kMaximumTailMs)
        idle_.store(true, std::memory_order_release);
    }
    // If the reserve is depleted, loop immediately and replenish it. DSP still
    // advances by exactly blockSize samples, never by wall time spent sleeping.
  }
}

bool RenderAheadEngine::performanceRequested(uint32_t now) const noexcept {
  if (transportActive_.load(std::memory_order_acquire) ||
      auditionNotesActive_.load(std::memory_order_acquire) ||
      mixPrinter_.isRecording())
    return true;
  const auto last =
      lastPerformanceActivityMs_.load(std::memory_order_acquire);
  if (last == 0)
    return false;
  const auto elapsed = static_cast<int32_t>(now - last);
  return elapsed >= 0 && static_cast<uint32_t>(elapsed) <= kPerformanceGraceMs;
}

int RenderAheadEngine::idleWaitMilliseconds(
    const AudioStreamRing &ring) noexcept {
  if (!(ring.sampleRate > 0.0) || ring.blockSize == 0)
    return 20;
  const auto read = ring.readFrame.load(std::memory_order_acquire);
  const auto write = ring.writeFrame.load(std::memory_order_acquire);
  if (write < read)
    return 1;
  const auto available = std::min(write - read, AudioStreamRing::capacity);
  const auto threshold = ring.targetFrames > ring.blockSize
                             ? ring.targetFrames - ring.blockSize
                             : uint32_t(0);
  if (available <= threshold)
    return 1;
  const auto frames = available - threshold;
  return juce::jlimit(
      1, 20, static_cast<int>(std::floor(1000.0 * frames / ring.sampleRate)));
}

uint64_t RenderAheadEngine::streamId() const noexcept {
  auto s = stream_.read(); return s.get() ? s.get()->ring->streamId : 0;
}
double RenderAheadEngine::reserveMs() const noexcept {
  auto s = stream_.read();
  return s.get() ? 1000.0 * s.get()->ring->targetFrames / s.get()->ring->sampleRate : 0;
}
juce::var RenderAheadEngine::diagnostics() const {
  auto *data = new juce::DynamicObject();
  data->setProperty("enabled", true);
  data->setProperty("controlOperations", static_cast<juce::int64>(AudioProcessingGate::controlCount()));
  data->setProperty("longestControlPauseMs", AudioProcessingGate::longestControlMs());
  data->setProperty("realtimeScheduling", realtime_.load(std::memory_order_relaxed));
  data->setProperty("renderWorkers", mixer_.renderWorkerCount());
  data->setProperty("requestedRenderWorkers", mixer_.requestedRenderWorkerCount());
  data->setProperty("helpersRealtime", mixer_.renderHelpersRealtime());
  data->setProperty("idle", idle_.load(std::memory_order_acquire));
  auto s = stream_.read();
  if (s.get()) {
    data->setProperty("controlWaitCount", static_cast<juce::int64>(s.get()->controlWaitCount.load(std::memory_order_relaxed)));
    data->setProperty("controlWaitMs", double(s.get()->controlWaitUs.load(std::memory_order_relaxed)) / 1000.0);
    const auto &ring = *s.get()->ring;
    const auto read = ring.readFrame.load(std::memory_order_acquire);
    const auto write = ring.writeFrame.load(std::memory_order_acquire);
    const auto available = write > read ? std::min(write - read, AudioStreamRing::capacity) : uint64_t(0);
    data->setProperty("streamId", juce::String(ring.streamId));
    data->setProperty("queuedFrames", static_cast<juce::int64>(available));
    data->setProperty("targetFrames", int(ring.targetFrames));
    data->setProperty("queuedMs", 1000.0 * double(available) / ring.sampleRate);
    data->setProperty("targetMs", 1000.0 * ring.targetFrames / ring.sampleRate);
    data->setProperty("renderedBlocks", static_cast<juce::int64>(s.get()->renderedBlocks.load()));
    data->setProperty("idleBlocks", static_cast<juce::int64>(s.get()->idleBlocks.load()));
    data->setProperty("skippedFrames", static_cast<juce::int64>(s.get()->skippedFrames.load()));
    data->setProperty("clockJumps", static_cast<juce::int64>(s.get()->clockJumps.load()));
    AudioStreamRing::Clock clock;
    data->setProperty("hostClockAgeMs", ring.readClock(clock) ? audioStreamTimeMs() - clock.timeMs : -1.0);
  }
  return juce::var(data);
}
} // namespace fiddle
