#include "FiddleProcessor.h"
#include "FiddleCIDs.h"
#include "FiddleController.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>

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

  // Add event (MIDI) input bus — 16 channels
  addEventInput(STR16("MIDI In"), 16);

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
  return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API FiddleProcessor::setActive(TBool state) {
  if (state) {
    // Create TCP relay on activation.
    // VST3 guarantees setActive is not called concurrently with process(),
    // so this is safe without additional synchronization.
    tcpRelay_ = std::make_unique<TcpRelay>();

    // Set up connection callback for state replay and UI notification.
    // The callback is invoked from the relay thread.
    tcpRelay_->setConnectionCallback([this](bool connected) {
      if (connected)
        replayProgramState();

      // Notify controller of connection status and program states (for UI)
      sendConnectionStatus(connected);
      sendProgramStatesToController();
    });

    // Reset transport tracking
    wasPlaying_ = false;
  } else {
    tcpRelay_.reset();
  }

  return AudioEffect::setActive(state);
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleProcessor::process(ProcessData &data) {
  // AUDIO THREAD — no blocking operations (no file I/O, no allocation,
  // no unbounded locks). pushMessage() uses a short mutex lock to enqueue.

  // Output silence
  if (data.numOutputs > 0) {
    for (int32 ch = 0; ch < data.outputs[0].numChannels; ++ch) {
      if (data.outputs[0].channelBuffers32[ch])
        std::memset(data.outputs[0].channelBuffers32[ch], 0,
                    data.numSamples * sizeof(float));
    }
    data.outputs[0].silenceFlags = (1ULL << data.outputs[0].numChannels) - 1;
  }

  // Get host position (needed by both parameter changes and event processing)
  int64 hostSamples = 0;
  bool isPlaying = false;
  if (data.processContext) {
    if (data.processContext->state & ProcessContext::kPlaying)
      isPlaying = true;
    if (data.processContext->state & ProcessContext::kProjectTimeMusicValid)
      hostSamples = data.processContext->projectTimeSamples;
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

      // Program change params: IDs 100..115 (kProgramParamBase + channel)
      if (paramId >= FiddleController::kProgramParamBase &&
          paramId < FiddleController::kProgramParamBase +
                        FiddleController::kNumChannels) {
        int ch = paramId - FiddleController::kProgramParamBase; // 0-based
        int program = static_cast<int>(
            value * (FiddleController::kNumPrograms - 1) + 0.5);

        channelStates_[ch].program = program;
        programStatesDirty_.store(true, std::memory_order_relaxed);

        if (tcpRelay_) {
          MidiEvent protoEvent;
          protoEvent.set_timestamp_samples(sampleOffset);
          protoEvent.set_host_sample_position(
              static_cast<uint64_t>(hostSamples + sampleOffset));
          protoEvent.set_channel(ch + 1); // 1-based

          auto *pc = protoEvent.mutable_program_change();
          pc->set_program_number(program);

          tcpRelay_->pushMessage(protoEvent);
        }
      }
      // Bank MSB params: IDs 200..215
      else if (paramId >= FiddleController::kBankMSBParamBase &&
               paramId < FiddleController::kBankMSBParamBase +
                             FiddleController::kNumChannels) {
        int ch = paramId - FiddleController::kBankMSBParamBase;
        channelStates_[ch].bankMSB = static_cast<int>(value * 127.0 + 0.5);
      }
      // Bank LSB params: IDs 300..315
      else if (paramId >= FiddleController::kBankLSBParamBase &&
               paramId < FiddleController::kBankLSBParamBase +
                             FiddleController::kNumChannels) {
        int ch = paramId - FiddleController::kBankLSBParamBase;
        channelStates_[ch].bankLSB = static_cast<int>(value * 127.0 + 0.5);
      }
      // Expression map CC params: IDs 400..591
      else if (paramId >= FiddleController::kCCParamBase &&
               paramId < FiddleController::kCCParamBase +
                             FiddleController::kNumCCs *
                                 FiddleController::kNumChannels) {
        int offset = paramId - FiddleController::kCCParamBase;
        int ccIndex = offset / FiddleController::kNumChannels;
        int ch = offset % FiddleController::kNumChannels;
        int ccNum = FiddleController::kFirstCC + ccIndex;
        int ccVal = static_cast<int>(value * 127.0 + 0.5);

        if (tcpRelay_) {
          MidiEvent protoEvent;
          protoEvent.set_timestamp_samples(sampleOffset);
          protoEvent.set_host_sample_position(
              static_cast<uint64_t>(hostSamples + sampleOffset));
          protoEvent.set_channel(ch + 1); // 1-based

          auto *ccMsg = protoEvent.mutable_cc();
          ccMsg->set_controller_number(ccNum);
          ccMsg->set_controller_value(ccVal);

          tcpRelay_->pushMessage(protoEvent);
        }

        pluginLog("CC" + std::to_string(ccNum) + " Ch" +
                  std::to_string(ch + 1) + " = " + std::to_string(ccVal));
      } else {
        // Log unrecognized parameter IDs so we can discover CCs
        pluginLog("Unhandled paramID=" + std::to_string(paramId) +
                  " value=" + std::to_string(value));
      }
    }
  }

  // Detect transport start
  if (isPlaying && !wasPlaying_ && tcpRelay_) {
    MidiEvent transportEvent;
    transportEvent.set_timestamp_samples(0);
    transportEvent.set_host_sample_position(static_cast<uint64_t>(hostSamples));

    auto *transport = transportEvent.mutable_transport();
    transport->set_type(MidiEvent_TransportEvent_Type_START);
    transport->set_host_sample_position(static_cast<uint64_t>(hostSamples));

    tcpRelay_->pushMessage(transportEvent);
  }
  wasPlaying_ = isPlaying;

  // Process MIDI events from input event list
  if (data.inputEvents)
    processEvents(data.inputEvents, hostSamples);

  // If program state changed this buffer, push to controller for UI.
  // This calls allocateMessage/sendMessage which allocate, but since this
  // plugin outputs silence (no audio synthesis), the overhead is acceptable.
  if (programStatesDirty_.exchange(false, std::memory_order_relaxed))
    sendProgramStatesToController();

  return kResultOk;
}

//----------------------------------------------------------------------
void FiddleProcessor::processEvents(IEventList *events, int64 hostSamples) {
  if (!tcpRelay_ || !events)
    return;

  int32 count = events->getEventCount();
  if (count > 0) {
    pluginLog("processEvents: " + std::to_string(count) + " events");
  }
  for (int32 i = 0; i < count; ++i) {
    Event event{};
    if (events->getEvent(i, event) != kResultOk)
      continue;

    pluginLog("Event type=" + std::to_string(event.type));

    MidiEvent protoEvent;
    protoEvent.set_timestamp_samples(event.sampleOffset);
    protoEvent.set_host_sample_position(
        static_cast<uint64_t>(hostSamples + event.sampleOffset));

    // Channel is stored per event type in the union, not at top level.
    // We extract it in each case below.

    switch (event.type) {
    case Event::kNoteOnEvent: {
      // VST3 channels are 0-based, our protobuf uses 1-based
      protoEvent.set_channel(event.noteOn.channel + 1);
      auto *noteOn = protoEvent.mutable_note_on();
      noteOn->set_note_number(event.noteOn.pitch);
      // VST3 velocity is 0-1 float, convert to 0-127
      noteOn->set_velocity(
          static_cast<uint32_t>(event.noteOn.velocity * 127.0f));
      break;
    }

    case Event::kNoteOffEvent: {
      protoEvent.set_channel(event.noteOff.channel + 1);
      auto *noteOff = protoEvent.mutable_note_off();
      noteOff->set_note_number(event.noteOff.pitch);
      noteOff->set_velocity(
          static_cast<uint32_t>(event.noteOff.velocity * 127.0f));
      break;
    }

    case Event::kPolyPressureEvent: {
      protoEvent.set_channel(event.polyPressure.channel + 1);
      auto *at = protoEvent.mutable_aftertouch();
      at->set_note_number(event.polyPressure.pitch);
      at->set_value(
          static_cast<uint32_t>(event.polyPressure.pressure * 127.0f));
      break;
    }

    case Event::kLegacyMIDICCOutEvent: {
      // This is how VST3 delivers CC, program change, pitch bend, etc.
      auto &cc = event.midiCCOut;
      protoEvent.set_channel(cc.channel + 1);

      if (cc.controlNumber <= 127) {
        // Standard CC
        auto *ccMsg = protoEvent.mutable_cc();
        ccMsg->set_controller_number(cc.controlNumber);
        ccMsg->set_controller_value(cc.value);

        // Track Bank Select
        int ch = cc.channel; // 0-based
        if (ch >= 0 && ch < 16) {
          if (cc.controlNumber == 0)
            channelStates_[ch].bankMSB = cc.value;
          else if (cc.controlNumber == 32)
            channelStates_[ch].bankLSB = cc.value;
        }
      } else if (cc.controlNumber == 129) {
        // kPitchBend
        auto *pb = protoEvent.mutable_pitch_bend();
        // VST3 pitch bend: value + value2*128 gives a 14-bit value
        pb->set_value(cc.value | (cc.value2 << 7));
      } else if (cc.controlNumber == 128) {
        // kAfterTouch (channel pressure)
        auto *cp = protoEvent.mutable_channel_pressure();
        cp->set_value(cc.value);
      } else if (cc.controlNumber == 130) {
        // kCtrlProgramChange — legacy MIDI program change
        auto *pc = protoEvent.mutable_program_change();
        pc->set_program_number(cc.value);

        int ch = cc.channel; // 0-based
        if (ch >= 0 && ch < 16) {
          channelStates_[ch].program = cc.value;
        }
      }
      break;
    }

    default:
      // Other event types — send as "other"
      auto *other = protoEvent.mutable_other();
      other->set_description("VST3 Event type=" + std::to_string(event.type));
      break;
    }

    tcpRelay_->pushMessage(protoEvent);
  }
}

//----------------------------------------------------------------------
void FiddleProcessor::replayProgramState() {
  // Called from the relay thread when the TCP connection is established.
  // Reads channelStates_ which may be concurrently written by the audio
  // thread. However, the values are plain ints and a torn read would at
  // worst send a stale program number — not cause UB or a crash.
  if (!tcpRelay_)
    return;

  // Replay all stored program states on connection
  for (int ch = 0; ch < 16; ++ch) {
    if (channelStates_[ch].program >= 0) {
      MidiEvent protoEvent;
      protoEvent.set_timestamp_samples(0);
      protoEvent.set_channel(ch + 1); // 1-based

      auto *pc = protoEvent.mutable_program_change();
      pc->set_program_number(channelStates_[ch].program);

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

  for (int ch = 0; ch < 16; ++ch) {
    int32 prog = -1;
    if (state->read(&prog, sizeof(int32)) != kResultOk)
      break;
    // Validate: program must be in [-1, 127], otherwise treat as unset.
    // Older or empty state streams may contain garbage values.
    if (prog < -1 || prog > 127)
      prog = -1;
    channelStates_[ch].program = prog;
  }

  // Push updated state to controller for UI display
  sendProgramStatesToController();

  return kResultOk;
}

tresult PLUGIN_API FiddleProcessor::getState(IBStream *state) {
  // Called from the main thread when saving state.
  // VST3 guarantees this is not called concurrently with process().
  if (!state)
    return kResultFalse;

  for (int ch = 0; ch < 16; ++ch) {
    int32 prog = channelStates_[ch].program;
    state->write(&prog, sizeof(int32));
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
  // Thread safety note: this writes channelStates_ from the message thread
  // while process() reads/writes it from the audio thread. For plain int
  // fields the worst case is a torn read/write of a value — no crash risk.
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
      int ch = static_cast<int>(channel); // 0-based
      if (ch >= 0 && ch < 16) {
        channelStates_[ch].program = static_cast<int>(program);

        // Send to TCP relay
        if (tcpRelay_) {
          MidiEvent protoEvent;
          protoEvent.set_timestamp_samples(0);
          protoEvent.set_channel(ch + 1); // 1-based

          auto *pc = protoEvent.mutable_program_change();
          pc->set_program_number(static_cast<uint32_t>(program));

          tcpRelay_->pushMessage(protoEvent);
        }
      }
    }
    return kResultOk;
  }

  return AudioEffect::notify(message);
}

//----------------------------------------------------------------------
void FiddleProcessor::sendConnectionStatus(bool connected) {
  // Send connection status to the controller for UI display.
  // This is called from the relay thread when connection state changes.
  if (auto msg = owned(allocateMessage())) {
    msg->setMessageID("ConnectionStatus");
    msg->getAttributes()->setInt("Connected", connected ? 1 : 0);
    sendMessage(msg);
  }
}

//----------------------------------------------------------------------
void FiddleProcessor::sendProgramStatesToController() {
  // Send all 16 channel program assignments to the controller for UI display.
  // Called from relay thread (connection callback) and main thread
  // (setState).
  if (auto msg = owned(allocateMessage())) {
    msg->setMessageID("ProgramStates");
    auto *attrs = msg->getAttributes();
    for (int ch = 0; ch < 16; ++ch) {
      // Attribute keys: "P0" through "P15"
      char key[4];
      snprintf(key, sizeof(key), "P%d", ch);
      attrs->setInt(key, channelStates_[ch].program);
    }
    sendMessage(msg);
  }
  programStatesDirty_.store(false, std::memory_order_relaxed);
}

} // namespace fiddle
