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
  // Program change params: kProgramParamBase + logicalChannel
  static constexpr Steinberg::Vst::ParamID kProgramParamBase = 100;

  // Selective CC params: only CCs we actually use.
  // paramID = kCCParamBase + ccIndex * kNumChannels + logicalChannel
  // 21 CCs × 256 channels = 5,376 params
  static constexpr Steinberg::Vst::ParamID kCCParamBase = 500;

  // 16 ports × 16 channels = 256 total channels.
  static constexpr int kNumPorts = 16;
  static constexpr int kChannelsPerPort = 16;
  static constexpr int kNumChannels = kNumPorts * kChannelsPerPort; // 256
  static constexpr int kNumPrograms = 128;

  // The CCs we register as VST3 parameters.
  // CC1 (Mod Wheel), CC7 (Volume), CC11 (Expression), CC102-CC119 (switches).
  static constexpr int kSupportedCCs[] = {1,   7,   11,  102, 103, 104, 105,
                                          106, 107, 108, 109, 110, 111, 112,
                                          113, 114, 115, 116, 117, 118, 119};
  static constexpr int kNumSupportedCCs =
      sizeof(kSupportedCCs) / sizeof(kSupportedCCs[0]); // 21

  /// Returns the dense index (0-20) for a CC number, or -1 if not supported.
  static int ccToIndex(int ccNum) {
    for (int i = 0; i < kNumSupportedCCs; ++i)
      if (kSupportedCCs[i] == ccNum)
        return i;
    return -1;
  }

  // --- Status queries for the UI ---
  bool isConnected() const { return isConnected_.load(); }
  int getChannelProgram(int ch) const;
  /// Returns the Fiddle preset name for a program number, or "" if unknown.
  std::string getInstrumentName(int program) const;
  /// Returns the config file basename (without path and extension).
  std::string getConfigName() const;

private:
  void sendProgramChangeToProcessor(int channel, int program);
  void loadPresetNames();

  std::atomic<bool> isConnected_{false};
  int channelPrograms_[kNumChannels];
  /// program number → human-readable name (parsed from presets.xml)
  std::map<int, std::string> programNames_;
  /// Config file path (received from processor)
  std::string configPath_;
};

} // namespace fiddle
