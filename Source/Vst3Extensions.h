#pragma once

#include "MidiTcpRelay.h"
#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

// VST3 SDK includes
#include <base/source/fobject.h>
#include <pluginterfaces/base/ustring.h>
#include <pluginterfaces/vst/ivstchannelcontextinfo.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstunits.h>

namespace fiddle {

class FiddleControllerExtensions : public Steinberg::FObject,
                                   public Steinberg::Vst::IMidiMapping,
                                   public Steinberg::Vst::IUnitInfo {
public:
  FiddleControllerExtensions(FiddleAudioProcessor &p) : processor(p) {}
  virtual ~FiddleControllerExtensions() {}

  // IMidiMapping
  Steinberg::tresult PLUGIN_API
  getMidiControllerAssignment(int32 busIndex, int16 channel,
                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                              Steinberg::Vst::ParamID &id) override {
    // Map CC 0 -> Bank MSB
    if (midiControllerNumber == 0) {
      id = FiddleAudioProcessor::kParamIdBankMSB;
      return Steinberg::kResultOk;
    }
    // Map CC 32 -> Bank LSB
    if (midiControllerNumber == 32) {
      id = FiddleAudioProcessor::kParamIdBankLSB;
      return Steinberg::kResultOk;
    }
    return Steinberg::kResultFalse;
  }

  // IUnitInfo
  int32 PLUGIN_API getUnitCount() override { return 1; }

  Steinberg::tresult PLUGIN_API
  getUnitInfo(int32 unitIndex, Steinberg::Vst::UnitInfo &info) override {
    if (unitIndex == 0) {
      info.id = 1; // Unit ID
      info.parentUnitId = Steinberg::Vst::kRootUnitId;
      Steinberg::UString(info.name, 128).fromAscii("Main Unit");
      info.programListId = 1; // Link to List 1 (Programs)
      return Steinberg::kResultOk;
    }
    return Steinberg::kResultFalse;
  }

  int32 PLUGIN_API getProgramListCount() override { return 1; }

  Steinberg::tresult PLUGIN_API getProgramListInfo(
      int32 listIndex, Steinberg::Vst::ProgramListInfo &info) override {
    if (listIndex == 0) {
      info.id = 1;
      Steinberg::UString(info.name, 128).fromAscii("Programs");
      info.programCount = 128;
      return Steinberg::kResultOk;
    }
    return Steinberg::kResultFalse;
  }

  Steinberg::tresult PLUGIN_API
  getProgramName(Steinberg::Vst::ProgramListID listId, int32 programIndex,
                 Steinberg::Vst::String128 name) override {
    juce::String progName = processor.getProgramName(programIndex);
    Steinberg::UString(name, 128).fromAscii(progName.toRawUTF8());
    return Steinberg::kResultOk;
  }

  Steinberg::tresult PLUGIN_API
  getProgramInfo(Steinberg::Vst::ProgramListID listId, int32 programIndex,
                 const char *attributeId,
                 Steinberg::Vst::String128 attributeValue) override {
    return Steinberg::kResultFalse;
  }

  Steinberg::tresult PLUGIN_API hasProgramPitchNames(
      Steinberg::Vst::ProgramListID listId, int32 programIndex) override {
    return Steinberg::kResultFalse;
  }
  Steinberg::tresult PLUGIN_API getProgramPitchName(
      Steinberg::Vst::ProgramListID listId, int32 programIndex, int16 midiPitch,
      Steinberg::Vst::String128 name) override {
    return Steinberg::kResultFalse;
  }
  Steinberg::Vst::UnitID PLUGIN_API getSelectedUnit() override { return 1; }
  Steinberg::tresult PLUGIN_API
  selectUnit(Steinberg::Vst::UnitID unitId) override {
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API getUnitByBus(
      Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
      int32 busIndex, int32 channel, Steinberg::Vst::UnitID &unitId) override {
    unitId = 1; // Default to Main Unit
    return Steinberg::kResultOk;
  }

  Steinberg::tresult PLUGIN_API
  setUnitProgramData(int32 listOrUnitId, int32 programIndex,
                     Steinberg::IBStream *data) override {
    return Steinberg::kResultFalse;
  }

  OBJ_METHODS(FiddleControllerExtensions, Steinberg::FObject)
  DEF_INTERFACES_2(Steinberg::Vst::IMidiMapping, Steinberg::Vst::IUnitInfo,
                   Steinberg::FObject)
  REFCOUNT_METHODS(Steinberg::FObject)

private:
  FiddleAudioProcessor &processor;
};

class FiddleVST3Extensions : public juce::VST3ClientExtensions {
public:
  FiddleVST3Extensions(FiddleAudioProcessor &p) : processor(p) {
    controllerExtensions = new FiddleControllerExtensions(p);
  }
  virtual ~FiddleVST3Extensions() override {
    if (controllerExtensions)
      controllerExtensions->release();
  }

  static juce::VST3ClientExtensions *get(juce::AudioProcessor *p) {
    if (auto *mp = dynamic_cast<FiddleAudioProcessor *>(p))
      return mp->getVST3ClientExtensions();
    return nullptr;
  }

  Steinberg::tresult queryIEditController(const Steinberg::TUID _iid,
                                          void **obj) override {
    if (controllerExtensions)
      return controllerExtensions->queryInterface(_iid, obj);
    return Steinberg::kResultFalse;
  }

private:
  FiddleAudioProcessor &processor;
  FiddleControllerExtensions *controllerExtensions = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FiddleVST3Extensions)
};

} // namespace fiddle
