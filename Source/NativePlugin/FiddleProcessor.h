#pragma once

#include "TcpRelay.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <memory>

namespace fiddle {

/**
 * FiddleProcessor: VST3 audio processor component.
 *
 * This receives MIDI events from the host via IEventList, converts them
 * to protobuf MidiEvent messages, and sends them over TCP to FiddleServer.
 *
 * It also processes "program change" messages from the controller
 * (sent via IMessage when the host changes a per-channel program parameter).
 */
class FiddleProcessor : public Steinberg::Vst::AudioEffect {
public:
  FiddleProcessor();
  ~FiddleProcessor() override;

  // IPluginBase
  Steinberg::tresult PLUGIN_API
  initialize(Steinberg::FUnknown *context) override;
  Steinberg::tresult PLUGIN_API terminate() override;

  // IAudioProcessor
  Steinberg::tresult PLUGIN_API setBusArrangements(
      Steinberg::Vst::SpeakerArrangement *inputs, Steinberg::int32 numIns,
      Steinberg::Vst::SpeakerArrangement *outputs,
      Steinberg::int32 numOuts) override;

  Steinberg::tresult PLUGIN_API
  setupProcessing(Steinberg::Vst::ProcessSetup &setup) override;

  Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;

  Steinberg::tresult PLUGIN_API
  process(Steinberg::Vst::ProcessData &data) override;

  // IComponent
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream *state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream *state) override;

  // IConnectionPoint (message from controller)
  Steinberg::tresult PLUGIN_API
  notify(Steinberg::Vst::IMessage *message) override;

  static Steinberg::FUnknown *createInstance(void *) {
    return static_cast<Steinberg::Vst::IAudioProcessor *>(
        new FiddleProcessor());
  }

private:
  void processEvents(Steinberg::Vst::IEventList *events,
                     Steinberg::int64 hostSamples);
  void replayProgramState();
  void sendConnectionStatus(bool connected);
  void sendProgramStatesToController();

  std::unique_ptr<TcpRelay> tcpRelay_;

  // Per-channel state
  struct ChannelState {
    int program = -1; // -1 = not set
    int bankMSB = 0;
    int bankLSB = 0;
  };
  std::array<ChannelState, 16> channelStates_;

  bool wasPlaying_ = false;

  // Set by process() when a program change is received, cleared after
  // sending update to controller. Checked by connection callback timer.
  std::atomic<bool> programStatesDirty_{false};
};

} // namespace fiddle
