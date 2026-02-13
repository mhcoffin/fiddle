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
 * This creates 17 units:
 *   - Root unit (id=0)
 *   - 16 channel units (ids 1-16), each with its own ProgramList
 *
 * Each channel unit has a kIsProgramChange parameter that the host
 * (Dorico) uses to select instruments per MIDI channel.
 *
 * Also implements IMidiMapping for CC0 (Bank MSB) and CC32 (Bank LSB).
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
  // Bank MSB params: 200 + channel (0-15) → IDs 200-215
  static constexpr Steinberg::Vst::ParamID kBankMSBParamBase = 200;
  // Bank LSB params: 300 + channel (0-15) → IDs 300-315
  static constexpr Steinberg::Vst::ParamID kBankLSBParamBase = 300;

  // Expression map CC params: per-CC base IDs.
  // CC N on channel C → paramID = kCCParamBase + (N - kFirstCC) * 16 + C
  // CCs 102-113 → 12 CCs × 16 channels = 192 params (IDs 400-591)
  static constexpr int kFirstCC = 102;
  static constexpr int kLastCC = 113;
  static constexpr int kNumCCs = kLastCC - kFirstCC + 1; // 12
  static constexpr Steinberg::Vst::ParamID kCCParamBase = 400;

  static constexpr int kNumChannels = 16;
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
  int channelPrograms_[16] = {-1, -1, -1, -1, -1, -1, -1, -1,
                              -1, -1, -1, -1, -1, -1, -1, -1};
  /// program number → human-readable name (parsed from presets.xml)
  std::map<int, std::string> programNames_;
};

} // namespace fiddle
