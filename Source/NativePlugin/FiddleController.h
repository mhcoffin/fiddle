#pragma once

#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <atomic>
#include <map>
#include <string>

namespace fiddle {

// GM Instrument names (defined in FiddleController.cpp, used by FiddlePlugView)
extern const char *kGMInstruments[128];

/**
 * FiddleController: VST3 edit controller with per-channel program changes.
 *
 * Creates 48 ports × 16 channels = 768 unit slots, each with a program
 * change parameter. The host (Dorico) uses these to select instruments
 * per MIDI channel/port.
 *
 * CC data arrives through the event buses directly — we don't expose
 * per-channel CC parameters to keep the parameter count manageable.
 */
class FiddleController : public Steinberg::Vst::EditControllerEx1,
                         public Steinberg::Vst::IMidiMapping {
public:
  FiddleController();
  ~FiddleController() override;

  // IPluginBase
  Steinberg::tresult PLUGIN_API
  initialize(Steinberg::FUnknown *context) override;
  Steinberg::tresult PLUGIN_API terminate() override;

  // EditController
  Steinberg::tresult PLUGIN_API
  setComponentState(Steinberg::IBStream *state) override;

  // IEditController
  Steinberg::tresult PLUGIN_API setParamNormalized(
      Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) override;

  // createView — return our custom UI
  Steinberg::IPlugView *PLUGIN_API
  createView(Steinberg::FIDString name) override;

  // IConnectionPoint — receive messages from processor
  Steinberg::tresult PLUGIN_API
  notify(Steinberg::Vst::IMessage *message) override;

  // IUnitInfo (override from EditControllerEx1)
  Steinberg::tresult PLUGIN_API
  getUnitByBus(Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
               Steinberg::int32 busIndex, Steinberg::int32 channel,
               Steinberg::Vst::UnitID &unitId) override;

  Steinberg::tresult PLUGIN_API getMidiControllerAssignment(
      Steinberg::int32 busIndex, Steinberg::int16 channel,
      Steinberg::Vst::CtrlNumber midiControllerNumber,
      Steinberg::Vst::ParamID &id) override;

  static Steinberg::FUnknown *createInstance(void *) {
    return static_cast<Steinberg::Vst::IEditController *>(
        new FiddleController());
  }

  // Interface support
  OBJ_METHODS(FiddleController, EditControllerEx1)
  DEFINE_INTERFACES
  DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
  END_DEFINE_INTERFACES(EditControllerEx1)
  REFCOUNT_METHODS(EditControllerEx1)

  // Parameter IDs
  // Program change params: 100 + channel (0-15) → IDs 100-115
  static constexpr Steinberg::Vst::ParamID kProgramParamBase = 100;

  // All CC params: paramID = kCCParamBase + ccNumber * kNumChannels + channel
  // CC 0-127 × 16 channels = 2048 params (IDs 200-2247)
  static constexpr Steinberg::Vst::ParamID kCCParamBase = 200;
  static constexpr int kNumCCs = 128;

  // 48 ports × 16 channels = 768 total. Dorico discovers multi-port
  // layout from the endpoint config (VE Pro-style).
  static constexpr int kNumPorts = 48;
  static constexpr int kChannelsPerPort = 16;
  static constexpr int kNumChannels = kNumPorts * kChannelsPerPort; // 768
  static constexpr int kNumPrograms = 128;

  // --- Status queries for the UI ---
  bool isConnected() const { return isConnected_.load(); }
  int getChannelProgram(int ch) const;
  /// Returns the Fiddle preset name for a program number, or "" if unknown.
  std::string getInstrumentName(int program) const;

private:
  void sendProgramChangeToProcessor(int channel, int program);
  void loadPresetNames();

  std::atomic<bool> isConnected_{false};
  int channelPrograms_[kNumChannels];
  /// program number → human-readable name (parsed from presets.xml)
  std::map<int, std::string> programNames_;
};

} // namespace fiddle
