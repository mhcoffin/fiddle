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
 * Creates 16 ports × 16 channels = 256 unit slots, each with a program
 * change parameter. The host (Dorico) uses these to select instruments
 * per MIDI channel/port.
 *
 * Selected CCs are exposed as per-channel VST3 parameters; the processor
 * merges all parameter points with the input event list by sample offset.
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

  // MIDI controller params, reachable only through IMidiMapping.
  // paramID = kCCParamBase + ccIndex * kNumChannels + logicalChannel
  static constexpr Steinberg::Vst::ParamID kCCParamBase = 500;

  // 16 ports × 16 channels = 256 total channels.
  static constexpr int kNumPorts = 16;
  static constexpr int kChannelsPerPort = 16;
  static constexpr int kNumChannels = kNumPorts * kChannelsPerPort; // 256
  static constexpr int kNumPrograms = 128;

  // The controllers we register as VST3 parameters. In VST3 a host delivers
  // CCs, channel pressure and pitch bend only through IMidiMapping, so any
  // controller missing here never reaches Fiddle (pedals, breath, bend...).
  // The first 21 keep their original indices, and so their parameter IDs.
  // Channel-mode messages (120-127) stay excluded: Dorico sends CC123
  // during playback and the server treats 120/123 as panic.
  static constexpr int kNumLegacyCCs = 21;
  static constexpr int kSupportedCCs[] = {
      1,   7,   11,  102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,
      113, 114, 115, 116, 117, 118, 119,
      // Added: every remaining CC 0-101, then channel pressure and bend.
      0,   2,   3,   4,   5,   6,   8,   9,   10,  12,  13,  14,  15,  16,
      17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
      31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
      45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,
      59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,
      73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,
      87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  99,  100,
      101, Steinberg::Vst::kAfterTouch, Steinberg::Vst::kPitchBend};
  static constexpr int kNumSupportedCCs =
      sizeof(kSupportedCCs) / sizeof(kSupportedCCs[0]); // 122

  /// Returns the dense index for a controller number, or -1 if unsupported.
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
  /// Returns the full config file path.
  std::string getConfigPath() const { return configPath_; }
  /// Returns the config version (ISO 8601 timestamp).
  std::string getConfigVersion() const { return configVersion_; }

private:
  void sendProgramChangeToProcessor(int channel, int program);
  void loadPresetNames();

  std::atomic<bool> isConnected_{false};
  int channelPrograms_[kNumChannels];
  /// program number → human-readable name (parsed from presets.xml)
  std::map<int, std::string> programNames_;
  /// Config file path (received from processor)
  std::string configPath_;
  /// Config version (ISO 8601 timestamp, received from processor)
  std::string configVersion_;
};

} // namespace fiddle
