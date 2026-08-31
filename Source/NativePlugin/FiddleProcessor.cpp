#include "FiddleProcessor.h"
#include "AudioConsumer.h"
#include "FiddleCIDs.h"
#include "FiddleController.h"
#include "ProgramStateReplay.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
// File-based logging for diagnostics — stderr is invisible inside Dorico
std::mutex logMutex;
void pluginLog(const std::string &msg) {
  std::lock_guard<std::mutex> lock(logMutex);
  std::ofstream f("/tmp/fiddle_plugin.log", std::ios::app);
  f << msg << std::endl;
}
} // namespace

namespace fiddle {

//----------------------------------------------------------------------
FiddleProcessor::FiddleProcessor() { setControllerClass(kFiddleControllerUID); }

FiddleProcessor::~FiddleProcessor() = default;

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleProcessor::initialize(FUnknown *context) {
  tresult result = AudioEffect::initialize(context);
  if (result != kResultOk)
    return result;

  // 48 event (MIDI) input buses — one per port, 16 channels each.
  // Dorico discovers and assigns instruments via the endpoint config.
  for (int p = 0; p < kNumPorts; ++p) {
    char busName[32];
    snprintf(busName, sizeof(busName), "Port %d", p + 1);
    String128 name128;
    UString(name128, 128).fromAscii(busName);
    addEventInput(name128, 16, kMain, BusInfo::kDefaultActive);
  }

  // Add stereo audio output (silent — we don't synthesize audio)
  addAudioOutput(STR16("Audio Out"), SpeakerArr::kStereo);

  return kResultOk;
}

tresult PLUGIN_API FiddleProcessor::terminate() {
  tcpRelay_.reset();
  return AudioEffect::terminate();
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleProcessor::setBusArrangements(
    SpeakerArrangement *inputs, int32 numIns, SpeakerArrangement *outputs,
    int32 numOuts) {
  // Accept any arrangement for output (we just output silence)
  if (numOuts >= 1 && outputs[0] == SpeakerArr::kStereo)
    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
  return kResultFalse;
}

tresult PLUGIN_API FiddleProcessor::setupProcessing(ProcessSetup &setup) {
  cachedSampleRate_.store(setup.sampleRate, std::memory_order_relaxed);

  // Initial latency uses default; TCP will push the real value once connected
  latencySamples_.store(static_cast<uint32>(
      setup.sampleRate * lastKnownDelayMs_.load(std::memory_order_relaxed) /
      1000.0), std::memory_order_relaxed);
  pluginLog("[Latency] setupProcessing: delayMs=" +
            std::to_string(lastKnownDelayMs_.load(std::memory_order_relaxed)) +
            " samples=" +
            std::to_string(latencySamples_.load(std::memory_order_relaxed)) +
            " sampleRate=" + std::to_string(setup.sampleRate));

  return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API FiddleProcessor::setActive(TBool state) {
  if (state) {
    // Create TCP relay on activation.
    // VST3 guarantees setActive is not called concurrently with process(),
    // so this is safe without additional synchronization.
    tcpRelay_ = std::make_unique<TcpRelay>(
        "127.0.0.1", 5252,
        lastKnownDelayMs_.load(std::memory_order_relaxed));

    tcpRelay_->setControlUpdateCallback(
        [this]() { scheduleControlFlush(); });

    // Set up connection callback for state replay and UI notification.
    // The callback is invoked from the relay thread.
    tcpRelay_->setConnectionCallback([this](bool connected) {
      if (connected) {
        replayProgramState();
      }

      // Marshal all host/controller work to the main queue. Calling
      // sendMessage() directly from the relay thread can deadlock the host.
      lastConnected_.store(connected, std::memory_order_relaxed);
      connectionChanged_.store(true, std::memory_order_release);
      programStatesDirty_.store(true, std::memory_order_relaxed);
      resetTempoRequested_.store(connected, std::memory_order_release);
      scheduleControlFlush();

      pluginLog("[Connection] " +
                std::string(connected ? "connected" : "disconnected") +
                " (deferred to main queue)");
    });

    // Start the worker here, outside the audio callback. It remains dormant
    // until process() marks this as the instance the host is actually using.
    tcpRelay_->start();
    relayStarted_ = false;

    // Reset transport tracking
    wasPlaying_ = false;
  } else {
    tcpRelay_.reset();
  }

  return AudioEffect::setActive(state);
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleProcessor::process(ProcessData &data) {
  // AUDIO THREAD — only bounded work and fixed-size lock-free handoff.

  // Activate the already-running relay on the first process() call.
  // This ensures only the active processor instance (the one Dorico
  // actually calls process() on) connects to the server.
  if (tcpRelay_ && !relayStarted_) {
    relayStarted_ = true;
    tcpRelay_->activate();
  }

  // Pull audio from FiddleServer via shared memory
  if (data.numOutputs > 0 && data.outputs[0].numChannels > 0) {
    audioConsumer_.pullAudio(data.outputs[0].channelBuffers32,
                             data.outputs[0].numChannels, data.numSamples);
    data.outputs[0].silenceFlags = 0;
  }

  if (resetTempoRequested_.exchange(false, std::memory_order_acquire))
    lastKnownBpm_ = -1.0;

  // Get host position (needed by both parameter changes and event processing)
  int64 hostSamples = 0;
  bool isPlaying = false;
  if (data.processContext) {
    if (data.processContext->state & ProcessContext::kPlaying)
      isPlaying = true;
    if (data.processContext->state & ProcessContext::kProjectTimeMusicValid) {
      hostSamples = data.processContext->projectTimeSamples;
      if (hostSamples < 0)
        hostSamples = 0; // Prevent uint64_t overflow
    }

    // Forward live tempo to FiddleServer when it changes.
    //
    // NOTE: Dorico's ProcessContext::tempo reports only the project's
    // initial/default tempo. Score-embedded tempo changes (rit., accel.,
    // tempo marks at specific bars) are NOT reflected in any ProcessContext
    // field — projectTimeMusic also advances at a constant rate regardless
    // of the score's tempo map. This is a known Dorico limitation.
    // We send ProcessContext::tempo as-is; it correctly reports the initial
    // project tempo but cannot detect mid-score changes.
    if (tcpRelay_ && data.numSamples > 0 &&
        (data.processContext->state & ProcessContext::kTempoValid)) {
      double bpm = data.processContext->tempo;
      if (bpm > 0.0 && std::abs(bpm - lastKnownBpm_) > 0.01) {
        lastKnownBpm_ = bpm;
        RealtimeMidiEvent tempoEvent;
        tempoEvent.type = RealtimeMidiEventType::Tempo;
        tempoEvent.hostSamplePosition = static_cast<uint64_t>(hostSamples);
        tempoEvent.tempo = bpm;
        if (data.processContext->state & ProcessContext::kTimeSigValid) {
          tempoEvent.data1 = data.processContext->timeSigNumerator;
          tempoEvent.data2 = data.processContext->timeSigDenominator;
        } else {
          tempoEvent.data1 = 4;
          tempoEvent.data2 = 4;
        }
        (void)tcpRelay_->pushRealtimeEvent(tempoEvent);
      }
    }
  }

  // Process parameter changes from host (program changes, bank select, etc.)
  // The host sends these via IParameterChanges in the audio processing path.
  if (data.inputParameterChanges) {
    int32 numParams = data.inputParameterChanges->getParameterCount();
    for (int32 p = 0; p < numParams; ++p) {
      auto *queue = data.inputParameterChanges->getParameterData(p);
      if (!queue)
        continue;

      ParamID paramId = queue->getParameterId();
      int32 numPoints = queue->getPointCount();
      if (numPoints <= 0)
        continue;

      // Get the last value (most recent)
      int32 sampleOffset = 0;
      ParamValue value = 0;
      queue->getPoint(numPoints - 1, sampleOffset, value);

      // Program change params: IDs 100..163 (kProgramParamBase +
      // logicalChannel)
      if (paramId >= FiddleController::kProgramParamBase &&
          paramId < FiddleController::kProgramParamBase +
                        FiddleController::kNumChannels) {
        int logicalCh =
            paramId - FiddleController::kProgramParamBase; // 0-based
        int program = static_cast<int>(
            value * (FiddleController::kNumPrograms - 1) + 0.5);

        channelStates_[logicalCh].program.store(program, std::memory_order_relaxed);
        programStatesDirty_.store(true, std::memory_order_relaxed);

        if (tcpRelay_) {
          RealtimeMidiEvent event;
          event.type = RealtimeMidiEventType::ProgramChange;
          event.timestampSamples = sampleOffset;
          event.hostSamplePosition =
              static_cast<uint64_t>(hostSamples + sampleOffset);
          event.port = logicalCh / 16;
          event.channel = logicalCh % 16 + 1;
          event.data1 = program;
          (void)tcpRelay_->pushRealtimeEvent(event);
        }
      }
      // CC params: kCCParamBase + ccIndex * kNumChannels + logicalCh
      else if (paramId >= FiddleController::kCCParamBase &&
               paramId < FiddleController::kCCParamBase +
                             FiddleController::kNumSupportedCCs *
                                 FiddleController::kNumChannels) {
        int offset = paramId - FiddleController::kCCParamBase;
        int ccIdx = offset / FiddleController::kNumChannels;
        int logicalCh = offset % FiddleController::kNumChannels;
        int ccNum = FiddleController::kSupportedCCs[ccIdx];
        int ccVal = static_cast<int>(value * 127.0 + 0.5);

        // Track Bank Select in channel state
        if (logicalCh >= 0 && logicalCh < kTotalChannels) {
          if (ccNum == 0)
            channelStates_[logicalCh].bankMSB.store(ccVal, std::memory_order_relaxed);
          else if (ccNum == 32)
            channelStates_[logicalCh].bankLSB.store(ccVal, std::memory_order_relaxed);
        }

        if (tcpRelay_) {
          RealtimeMidiEvent event;
          event.type = RealtimeMidiEventType::ControlChange;
          event.timestampSamples = sampleOffset;
          event.hostSamplePosition =
              static_cast<uint64_t>(hostSamples + sampleOffset);
          event.port = logicalCh / 16;
          event.channel = logicalCh % 16 + 1;
          event.data1 = ccNum;
          event.data2 = ccVal;
          (void)tcpRelay_->pushRealtimeEvent(event);
        }
      }
    }
  }

  // Detect transport start
  if (isPlaying && !wasPlaying_ && tcpRelay_) {
    RealtimeMidiEvent event;
    event.type = RealtimeMidiEventType::TransportStart;
    event.hostSamplePosition = static_cast<uint64_t>(hostSamples);
    (void)tcpRelay_->pushRealtimeEvent(event);
  }

  // Detect transport stop
  if (!isPlaying && wasPlaying_ && tcpRelay_) {
    RealtimeMidiEvent event;
    event.type = RealtimeMidiEventType::TransportStop;
    event.hostSamplePosition = static_cast<uint64_t>(hostSamples);
    (void)tcpRelay_->pushRealtimeEvent(event);
  }
  wasPlaying_ = isPlaying;

  // Process MIDI events from input event list
  if (data.inputEvents)
    processEvents(data.inputEvents, hostSamples);

  if (programStatesDirty_.load(std::memory_order_relaxed) && tcpRelay_)
    tcpRelay_->requestControlUpdate();

  return kResultOk;
}

//----------------------------------------------------------------------
void FiddleProcessor::processEvents(IEventList *events, int64 hostSamples) {
  if (!tcpRelay_ || !events)
    return;

  int32 count = events->getEventCount();
  for (int32 i = 0; i < count; ++i) {
    Event event{};
    if (events->getEvent(i, event) != kResultOk)
      continue;

    RealtimeMidiEvent outgoing;
    outgoing.timestampSamples = event.sampleOffset;
    outgoing.hostSamplePosition =
        static_cast<uint64_t>(hostSamples + event.sampleOffset);

    // Compute logical channel from busIndex + per-event channel.
    // busIndex identifies the port (0-based), event channel is 0-15.
    int eventBus = event.busIndex;
    if (eventBus < 0 || eventBus >= kNumPorts)
      eventBus = 0;
    outgoing.port = eventBus;

    switch (event.type) {
    case Event::kNoteOnEvent: {
      outgoing.type = RealtimeMidiEventType::NoteOn;
      outgoing.channel = event.noteOn.channel + 1;
      outgoing.data1 = event.noteOn.pitch;
      outgoing.data2 =
          static_cast<int32_t>(event.noteOn.velocity * 127.0f);
      break;
    }

    case Event::kNoteOffEvent: {
      outgoing.type = RealtimeMidiEventType::NoteOff;
      outgoing.channel = event.noteOff.channel + 1;
      outgoing.data1 = event.noteOff.pitch;
      outgoing.data2 =
          static_cast<int32_t>(event.noteOff.velocity * 127.0f);
      break;
    }

    case Event::kPolyPressureEvent: {
      outgoing.type = RealtimeMidiEventType::PolyPressure;
      outgoing.channel = event.polyPressure.channel + 1;
      outgoing.data1 = event.polyPressure.pitch;
      outgoing.data2 =
          static_cast<int32_t>(event.polyPressure.pressure * 127.0f);
      break;
    }

    case Event::kLegacyMIDICCOutEvent: {
      // This is how VST3 delivers CC, program change, pitch bend, etc.
      auto &cc = event.midiCCOut;
      outgoing.channel = cc.channel + 1;

      if (cc.controlNumber <= 127) {
        outgoing.type = RealtimeMidiEventType::ControlChange;
        outgoing.data1 = cc.controlNumber;
        outgoing.data2 = cc.value;

        // Track Bank Select
        int logicalCh = eventBus * 16 + cc.channel;
        if (logicalCh >= 0 && logicalCh < kTotalChannels) {
          if (cc.controlNumber == 0)
            channelStates_[logicalCh].bankMSB.store(cc.value, std::memory_order_relaxed);
          else if (cc.controlNumber == 32)
            channelStates_[logicalCh].bankLSB.store(cc.value, std::memory_order_relaxed);
        }
      } else if (cc.controlNumber == 129) {
        outgoing.type = RealtimeMidiEventType::PitchBend;
        outgoing.data1 = cc.value | (cc.value2 << 7);
      } else if (cc.controlNumber == 128) {
        outgoing.type = RealtimeMidiEventType::ChannelPressure;
        outgoing.data1 = cc.value;
      } else if (cc.controlNumber == 130) {
        outgoing.type = RealtimeMidiEventType::ProgramChange;
        outgoing.data1 = cc.value;

        int logicalCh = eventBus * 16 + cc.channel;
        if (logicalCh >= 0 && logicalCh < kTotalChannels) {
          channelStates_[logicalCh].program.store(cc.value, std::memory_order_relaxed);
        }
      }
      break;
    }

    default:
      outgoing.type = RealtimeMidiEventType::Other;
      outgoing.data1 = event.type;
      break;
    }

    (void)tcpRelay_->pushRealtimeEvent(outgoing);
  }
}

//----------------------------------------------------------------------
void FiddleProcessor::replayProgramState() {
  // Called from the relay thread when the TCP connection is established.
  // Reads channelStates_ which may be concurrently written by the audio
  // thread. Atomic state avoids a data race; at worst the relay sends a stale
  // value which will be superseded by the next program change.
  if (!tcpRelay_)
    return;

  // Replay all stored program states on connection
  for (int ch = 0; ch < kTotalChannels; ++ch) {
    if (channelStates_[ch].program.load(std::memory_order_relaxed) >= 0) {
      auto protoEvent = makeProgramStateReplayEvent(
          ch, channelStates_[ch].program.load(std::memory_order_relaxed));
      tcpRelay_->pushMessage(protoEvent);
    }
  }
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleProcessor::setState(IBStream *state) {
  // Called from the main thread when loading state.
  // VST3 guarantees this is not called concurrently with process().
  if (!state)
    return kResultFalse;

  for (int ch = 0; ch < kTotalChannels; ++ch) {
    int32 prog = -1;
    if (state->read(&prog, sizeof(int32)) != kResultOk)
      break;
    // Validate: program must be in [-1, 127], otherwise treat as unset.
    // Older or empty state streams may contain garbage values.
    if (prog < -1 || prog > 127)
      prog = -1;
    channelStates_[ch].program.store(prog, std::memory_order_relaxed);
  }

  // Read config path (appended after program state)
  // Format: 4-byte length prefix + UTF-8 string
  int32 pathLen = 0;
  if (state->read(&pathLen, sizeof(int32)) == kResultOk && pathLen > 0 &&
      pathLen < 4096) {
    std::vector<char> buf(pathLen);
    if (state->read(buf.data(), pathLen) == kResultOk) {
      configPath_.assign(buf.data(), pathLen);
    }
  }

  // Read config version (length-prefixed, added after config path)
  int32 versionLen = 0;
  if (state->read(&versionLen, sizeof(int32)) == kResultOk && versionLen > 0 &&
      versionLen < 4096) {
    std::vector<char> vbuf(versionLen);
    if (state->read(vbuf.data(), versionLen) == kResultOk) {
      configVersion_.assign(vbuf.data(), versionLen);
    }
  }

  // Push updated state to controller for UI display
  sendConfigToController();
  sendProgramStatesToController();

  return kResultOk;
}

tresult PLUGIN_API FiddleProcessor::getState(IBStream *state) {
  // Called from the main thread when saving state.
  // VST3 guarantees this is not called concurrently with process().
  if (!state)
    return kResultFalse;

  for (int ch = 0; ch < kTotalChannels; ++ch) {
    int32 prog = channelStates_[ch].program.load(std::memory_order_relaxed);
    state->write(&prog, sizeof(int32));
  }

  // Write config path (length-prefixed)
  int32 pathLen = static_cast<int32>(configPath_.size());
  state->write(&pathLen, sizeof(int32));
  if (pathLen > 0) {
    state->write(configPath_.data(), pathLen);
  }

  // Write config version (length-prefixed)
  int32 versionLen = static_cast<int32>(configVersion_.size());
  state->write(&versionLen, sizeof(int32));
  if (versionLen > 0) {
    state->write(configVersion_.data(), versionLen);
  }

  return kResultOk;
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleProcessor::notify(IMessage *message) {
  // Called from the message/UI thread. Since program changes now arrive
  // via inputParameterChanges in process(), this path is no longer the
  // primary mechanism. However, we keep it as a fallback for any hosts
  // that might use the IMessage path.
  //
  // Channel state is atomic because this fallback can race the audio path.
  if (!message)
    return kInvalidArgument;

  const char *msgId = message->getMessageID();
  if (msgId && std::strcmp(msgId, "ProgramChange") == 0) {
    auto *attrs = message->getAttributes();
    if (!attrs)
      return kResultFalse;

    int64 channel = 0, program = 0;
    if (attrs->getInt("Channel", channel) == kResultOk &&
        attrs->getInt("Program", program) == kResultOk) {
      int ch = static_cast<int>(channel); // 0-based logical channel
      if (ch >= 0 && ch < kTotalChannels) {
        channelStates_[ch].program.store(static_cast<int>(program), std::memory_order_relaxed);

        // Send to TCP relay
        if (tcpRelay_) {
          tcpRelay_->pushMessage(
              makeProgramStateReplayEvent(ch, static_cast<int>(program)));
        }
      }
    }
    return kResultOk;
  }

  return AudioEffect::notify(message);
}

//----------------------------------------------------------------------
void FiddleProcessor::scheduleControlFlush() {
  if (controlFlushScheduled_.exchange(true, std::memory_order_acq_rel))
    return;

#ifdef __APPLE__
  addRef();
  dispatch_async_f(dispatch_get_main_queue(), this, [](void *context) {
    auto *processor = static_cast<FiddleProcessor *>(context);
    processor->flushControlUpdates();
    processor->release();
  });
#else
  flushControlUpdates();
#endif
}

void FiddleProcessor::flushControlUpdates() {
  controlFlushScheduled_.store(false, std::memory_order_release);

  if (connectionChanged_.exchange(false, std::memory_order_acquire)) {
    const bool connected = lastConnected_.load(std::memory_order_relaxed);
    sendConnectionStatus(connected);
    if (connected) {
      audioConsumer_.remap();
      announceConfigToServer();
    }
    sendConfigToController();
  }

  if (tcpRelay_ && tcpRelay_->consumeLatencyChanged()) {
    const int newDelay = tcpRelay_->getDelayMs();
    lastKnownDelayMs_.store(newDelay, std::memory_order_relaxed);
    latencySamples_.store(
        static_cast<uint32>(cachedSampleRate_.load(std::memory_order_relaxed) *
                            newDelay / 1000.0),
        std::memory_order_relaxed);
    if (auto *msg = allocateMessage()) {
      msg->setMessageID("LatencyChanged");
      sendMessage(msg);
      msg->release();
    }
  }

  if (tcpRelay_ && tcpRelay_->consumeConfigChanged()) {
    auto name = tcpRelay_->getConfigName();
    auto version = tcpRelay_->getConfigVersion();
    if (!name.empty())
      configPath_ = std::move(name);
    if (!version.empty())
      configVersion_ = std::move(version);
    sendConfigToController();
  }

  if (programStatesDirty_.exchange(false, std::memory_order_acq_rel))
    sendProgramStatesToController();
}

//----------------------------------------------------------------------
void FiddleProcessor::sendConnectionStatus(bool connected) {
  // Send connection status to the controller for UI display.
  // This is called on the main queue after connection state changes.
  if (auto msg = owned(allocateMessage())) {
    msg->setMessageID("ConnectionStatus");
    msg->getAttributes()->setInt("Connected", connected ? 1 : 0);
    sendMessage(msg);
  }
}

//----------------------------------------------------------------------
void FiddleProcessor::sendProgramStatesToController() {
  // Send all channel program assignments to the controller for UI display.
  // Called on the main thread.
  if (auto msg = owned(allocateMessage())) {
    msg->setMessageID("ProgramStates");
    auto *attrs = msg->getAttributes();
    for (int ch = 0; ch < kTotalChannels; ++ch) {
      // Attribute keys: "P0" through "P767"
      char key[16];
      snprintf(key, sizeof(key), "P%d", ch);
      attrs->setInt(key, channelStates_[ch].program.load(std::memory_order_relaxed));
    }
    sendMessage(msg);
  }
  programStatesDirty_.store(false, std::memory_order_relaxed);
}

//----------------------------------------------------------------------
void FiddleProcessor::sendConfigToController() {
  if (auto msg = owned(allocateMessage())) {
    msg->setMessageID("ConfigPath");
    TChar wpath[1024] = {};
    Steinberg::UString(wpath, 1024).fromAscii(configPath_.c_str());
    msg->getAttributes()->setString("Path", wpath);

    TChar wversion[1024] = {};
    Steinberg::UString(wversion, 1024).fromAscii(configVersion_.c_str());
    msg->getAttributes()->setString("Version", wversion);

    sendMessage(msg);
  }
}

//----------------------------------------------------------------------
void FiddleProcessor::announceConfigToServer() {
  if (!tcpRelay_ || configPath_.empty())
    return;

  MidiEvent hello;
  hello.set_timestamp_samples(0);
  hello.mutable_load_config()->set_config_path(configPath_);
  tcpRelay_->pushMessage(hello);

  pluginLog("Announced config to server: " + configPath_);
}

} // namespace fiddle
