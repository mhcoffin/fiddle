#include "FiddleController.h"
#include "FiddleCIDs.h"
#include "FiddlePlugView.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstcomponent.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace fiddle {

// GM Instrument names for program lists (non-static, extern in header)
const char *kGMInstruments[128] = {
    // Piano (0-7)
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano",
    "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord",
    "Clavi",
    // Chromatic Percussion (8-15)
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba",
    "Xylophone", "Tubular Bells", "Dulcimer",
    // Organ (16-23)
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
    "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    // Guitar (24-31)
    "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)",
    "Electric Guitar (jazz)", "Electric Guitar (clean)",
    "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar",
    "Guitar harmonics",
    // Bass (32-39)
    "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)",
    "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1",
    "Synth Bass 2",
    // Strings (40-47)
    "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings",
    "Pizzicato Strings", "Orchestral Harp", "Timpani",
    // Ensemble (48-55)
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1",
    "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice",
    "Orchestra Hit",
    // Brass (56-63)
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn",
    "Brass Section", "Synth Brass 1", "Synth Brass 2",
    // Reed (64-71)
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe",
    "English Horn", "Bassoon", "Clarinet",
    // Pipe (72-79)
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi",
    "Whistle", "Ocarina",
    // Synth Lead (80-87)
    "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)",
    "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)",
    "Lead 8 (bass + lead)",
    // Synth Pad (88-95)
    "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
    "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
    // Synth Effects (96-103)
    "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
    "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    // Ethnic (104-111)
    "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bag pipe", "Fiddle",
    "Shanai",
    // Percussive (112-119)
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum",
    "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    // Sound Effects (120-127)
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
    "Telephone Ring", "Helicopter", "Applause", "Gunshot"};

//----------------------------------------------------------------------
FiddleController::FiddleController() = default;
FiddleController::~FiddleController() = default;

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleController::initialize(FUnknown *context) {
  tresult result = EditControllerEx1::initialize(context);
  if (result != kResultOk)
    return result;

  // Load instrument names from Dorico's presets.xml
  loadPresetNames();

  // Create root unit (required, id=0)
  auto *rootUnit = new Unit(STR16("Root"), kRootUnitId, kNoParentUnitId);
  addUnit(rootUnit);

  // Create 16 channel units, each with its own program list
  for (int ch = 0; ch < kNumChannels; ++ch) {
    UnitID unitId = ch + 1;        // Units 1-16
    ProgramListID listId = ch + 1; // Program lists 1-16

    // Build unit name: "Channel 1" through "Channel 16"
    char unitName[32];
    snprintf(unitName, sizeof(unitName), "Channel %d", ch + 1);
    String128 unitName128;
    UString(unitName128, 128).fromAscii(unitName);

    auto *unit = new Unit(unitName128, unitId, kRootUnitId, listId);
    addUnit(unit);

    // Create program list for this channel
    char listName[32];
    snprintf(listName, sizeof(listName), "Ch %d Programs", ch + 1);
    String128 listName128;
    UString(listName128, 128).fromAscii(listName);

    auto *programList = new ProgramList(listName128, listId, unitId);
    for (int p = 0; p < kNumPrograms; ++p) {
      String128 progName;
      UString(progName, 128).fromAscii(kGMInstruments[p]);
      programList->addProgram(progName);
    }
    addProgramList(programList);

    // The ProgramList::getParameter() creates a Parameter with
    // kIsProgramChange flag — this is what Dorico looks for!
    // We register it manually with the correct param ID.
    ParamID programParamId = kProgramParamBase + ch;

    // Create the program change parameter manually with kIsProgramChange
    ParameterInfo paramInfo{};
    paramInfo.id = programParamId;
    UString(paramInfo.title, 128).fromAscii(unitName);
    UString(paramInfo.shortTitle, 128).fromAscii(unitName);
    paramInfo.units[0] = 0;
    paramInfo.stepCount = kNumPrograms - 1; // 127 steps for 128 programs
    paramInfo.defaultNormalizedValue = 0.0;
    paramInfo.unitId = unitId;
    paramInfo.flags =
        ParameterInfo::kIsProgramChange | ParameterInfo::kCanAutomate;

    parameters.addParameter(new Parameter(paramInfo));

    // Also create Bank MSB and Bank LSB parameters per channel
    {
      ParameterInfo bankMSBInfo{};
      bankMSBInfo.id = kBankMSBParamBase + ch;
      char title[32];
      snprintf(title, sizeof(title), "Bank MSB Ch%d", ch + 1);
      UString(bankMSBInfo.title, 128).fromAscii(title);
      UString(bankMSBInfo.shortTitle, 128).fromAscii(title);
      bankMSBInfo.stepCount = 127;
      bankMSBInfo.defaultNormalizedValue = 0.0;
      bankMSBInfo.unitId = unitId;
      bankMSBInfo.flags = ParameterInfo::kCanAutomate;
      parameters.addParameter(new Parameter(bankMSBInfo));
    }

    {
      ParameterInfo bankLSBInfo{};
      bankLSBInfo.id = kBankLSBParamBase + ch;
      char title[32];
      snprintf(title, sizeof(title), "Bank LSB Ch%d", ch + 1);
      UString(bankLSBInfo.title, 128).fromAscii(title);
      UString(bankLSBInfo.shortTitle, 128).fromAscii(title);
      bankLSBInfo.stepCount = 127;
      bankLSBInfo.defaultNormalizedValue = 0.0;
      bankLSBInfo.unitId = unitId;
      bankLSBInfo.flags = ParameterInfo::kCanAutomate;
      parameters.addParameter(new Parameter(bankLSBInfo));
    }

    // Create expression map CC parameters (CC102-CC113) per channel
    for (int cc = kFirstCC; cc <= kLastCC; ++cc) {
      ParameterInfo ccInfo{};
      ccInfo.id = kCCParamBase + (cc - kFirstCC) * kNumChannels + ch;
      char title[32];
      snprintf(title, sizeof(title), "CC%d Ch%d", cc, ch + 1);
      UString(ccInfo.title, 128).fromAscii(title);
      UString(ccInfo.shortTitle, 128).fromAscii(title);
      ccInfo.stepCount = 127;
      ccInfo.defaultNormalizedValue = 0.0;
      ccInfo.unitId = unitId;
      ccInfo.flags = ParameterInfo::kCanAutomate;
      parameters.addParameter(new Parameter(ccInfo));
    }

    // Create CC1 (dynamics) parameter per channel
    {
      ParameterInfo cc1Info{};
      cc1Info.id = kCC1ParamBase + ch;
      char title[32];
      snprintf(title, sizeof(title), "CC1 Ch%d", ch + 1);
      UString(cc1Info.title, 128).fromAscii(title);
      UString(cc1Info.shortTitle, 128).fromAscii(title);
      cc1Info.stepCount = 127;
      cc1Info.defaultNormalizedValue = 0.0;
      cc1Info.unitId = unitId;
      cc1Info.flags = ParameterInfo::kCanAutomate;
      parameters.addParameter(new Parameter(cc1Info));
    }
  }

  return kResultOk;
}

tresult PLUGIN_API FiddleController::terminate() {
  return EditControllerEx1::terminate();
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleController::setComponentState(IBStream *state) {
  // Read the processor's saved per-channel program state
  if (!state)
    return kResultFalse;

  for (int ch = 0; ch < kNumChannels; ++ch) {
    int32 prog = -1;
    if (state->read(&prog, sizeof(int32)) != kResultOk)
      break;

    if (prog >= 0 && prog < kNumPrograms) {
      ParamID paramId = kProgramParamBase + ch;
      ParamValue normalized = static_cast<ParamValue>(prog) /
                              static_cast<ParamValue>(kNumPrograms - 1);
      EditControllerEx1::setParamNormalized(paramId, normalized);
    }
  }

  return kResultOk;
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleController::setParamNormalized(ParamID tag,
                                                        ParamValue value) {
  // Intercept program change parameter updates to notify the processor
  if (tag >= kProgramParamBase && tag < kProgramParamBase + kNumChannels) {
    int channel = tag - kProgramParamBase; // 0-based
    int program = static_cast<int>(value * (kNumPrograms - 1) + 0.5);

    // Forward to processor via IMessage
    sendProgramChangeToProcessor(channel, program);
  }

  return EditControllerEx1::setParamNormalized(tag, value);
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleController::getUnitByBus(MediaType type,
                                                  BusDirection dir,
                                                  int32 busIndex, int32 channel,
                                                  UnitID &unitId) {
  // Map event (MIDI) bus channel to the corresponding channel unit
  if (type == kEvent && dir == kInput && busIndex == 0) {
    if (channel >= 0 && channel < kNumChannels) {
      unitId = channel + 1; // Unit IDs 1-16 for channels 0-15
      return kResultOk;
    }
  }

  return kResultFalse;
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleController::getMidiControllerAssignment(
    int32 busIndex, int16 channel, CtrlNumber midiControllerNumber,
    ParamID &id) {
  if (busIndex != 0)
    return kResultFalse;

  if (channel < 0 || channel >= kNumChannels)
    return kResultFalse;

  switch (midiControllerNumber) {
  case 0: // CC 0 = Bank MSB
    id = kBankMSBParamBase + channel;
    return kResultOk;
  case 1: // CC 1 = Mod wheel / dynamics
    id = kCC1ParamBase + channel;
    return kResultOk;
  case 32: // CC 32 = Bank LSB
    id = kBankLSBParamBase + channel;
    return kResultOk;
  default:
    // Expression map CCs 102-113
    if (midiControllerNumber >= kFirstCC && midiControllerNumber <= kLastCC) {
      id = kCCParamBase + (midiControllerNumber - kFirstCC) * kNumChannels +
           channel;
      return kResultOk;
    }
    return kResultFalse;
  }
}

//----------------------------------------------------------------------
void FiddleController::sendProgramChangeToProcessor(int channel, int program) {
  if (auto *handler = getComponentHandler()) {
    // Use IMessage to communicate with the processor
    if (auto *peer = getPeer()) {
      if (auto msg = owned(allocateMessage())) {
        msg->setMessageID("ProgramChange");
        msg->getAttributes()->setInt("Channel", channel);
        msg->getAttributes()->setInt("Program", program);
        sendMessage(msg);
      }
    }
  }
}

//----------------------------------------------------------------------
IPlugView *PLUGIN_API FiddleController::createView(FIDString name) {
  if (name && strcmp(name, ViewType::kEditor) == 0)
    return new FiddlePlugView(this);
  return nullptr;
}

//----------------------------------------------------------------------
tresult PLUGIN_API FiddleController::notify(IMessage *message) {
  if (!message)
    return kInvalidArgument;

  const char *msgId = message->getMessageID();

  if (msgId && strcmp(msgId, "ConnectionStatus") == 0) {
    int64 connected = 0;
    if (message->getAttributes()->getInt("Connected", connected) == kResultOk) {
      isConnected_ = (connected != 0);
    }
    return kResultOk;
  }

  if (msgId && strcmp(msgId, "ProgramStates") == 0) {
    auto *attrs = message->getAttributes();
    for (int ch = 0; ch < kNumChannels; ++ch) {
      char key[4];
      snprintf(key, sizeof(key), "P%d", ch);
      int64 prog = -1;
      if (attrs->getInt(key, prog) == kResultOk) {
        // Only accept valid program range
        if (prog >= 0 && prog < kNumPrograms)
          channelPrograms_[ch] = static_cast<int>(prog);
        else
          channelPrograms_[ch] = -1;
      }
    }
    return kResultOk;
  }

  return EditControllerEx1::notify(message);
}

//----------------------------------------------------------------------
int FiddleController::getChannelProgram(int ch) const {
  if (ch >= 0 && ch < kNumChannels)
    return channelPrograms_[ch];
  return -1;
}

//----------------------------------------------------------------------
std::string FiddleController::getInstrumentName(int program) const {
  auto it = programNames_.find(program);
  if (it != programNames_.end())
    return it->second;
  return "";
}

//----------------------------------------------------------------------
void FiddleController::loadPresetNames() {
  // Read presets.xml from Dorico's PluginPresetLibraries directory.
  // Path: ~/Library/Application Support/Steinberg/Dorico 6/
  //       PluginPresetLibraries/Fiddle/presets.xml
  const char *home = std::getenv("HOME");
  if (!home)
    return;

  std::string path = std::string(home) +
                     "/Library/Application Support/Steinberg/Dorico 6/"
                     "PluginPresetLibraries/Fiddle/presets.xml";

  std::ifstream file(path);
  if (!file.is_open())
    return;

  std::stringstream buf;
  buf << file.rdbuf();
  std::string xml = buf.str();

  // Simple XML parsing: find each <Preset>...</Preset> block,
  // extract <Name> and <Program> values.
  size_t pos = 0;
  while (true) {
    size_t presetStart = xml.find("<Preset>", pos);
    if (presetStart == std::string::npos)
      break;
    size_t presetEnd = xml.find("</Preset>", presetStart);
    if (presetEnd == std::string::npos)
      break;

    std::string block = xml.substr(presetStart, presetEnd - presetStart);

    // Extract <Name>...</Name>
    std::string name;
    size_t nameStart = block.find("<Name>");
    size_t nameEnd = block.find("</Name>");
    if (nameStart != std::string::npos && nameEnd != std::string::npos) {
      nameStart += 6; // strlen("<Name>")
      name = block.substr(nameStart, nameEnd - nameStart);
    }

    // Extract <Program>...</Program>
    int prog = -1;
    size_t progStart = block.find("<Program>");
    size_t progEnd = block.find("</Program>");
    if (progStart != std::string::npos && progEnd != std::string::npos) {
      progStart += 9; // strlen("<Program>")
      prog = std::atoi(block.substr(progStart, progEnd - progStart).c_str());
    }

    if (prog >= 0 && !name.empty()) {
      // Strip "Fiddle_" prefix if present for cleaner display
      const std::string prefix = "Fiddle_";
      if (name.compare(0, prefix.size(), prefix) == 0)
        name = name.substr(prefix.size());
      programNames_[prog] = name;
    }

    pos = presetEnd + 9;
  }
}

} // namespace fiddle
