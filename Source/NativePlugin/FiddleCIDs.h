#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace fiddle {

// These FUIDs MUST match the existing JUCE-built Fiddle plugin so that
// Dorico playback templates continue to work.
//
// Extracted from the existing Fiddle.vst3/Contents/Resources/moduleinfo.json:
//   Component:  ABCDEF01-9182FAEB-4D616E75-4669646C
//   Controller: ABCDEF01-1234ABCD-4D616E75-4669646C

#ifdef FIDDLE_MIDI_PROBE
// Deliberately distinct: installing the probe cannot replace a working Fiddle.
static const Steinberg::FUID kFiddleProcessorUID(0x472C6B18, 0x0B184E76, 0x93E656AA, 0x50E8CF21);
static const Steinberg::FUID kFiddleControllerUID(0x71B31943, 0xB1204DD5, 0xAF83A6F1, 0x25E90792);
#else
static const Steinberg::FUID kFiddleProcessorUID(0xABCDEF01, 0x9182FAEB,
                                                 0x4D616E75, 0x4669646C);

static const Steinberg::FUID kFiddleControllerUID(0xABCDEF01, 0x1234ABCD,
                                                  0x4D616E75, 0x4669646C);
#endif

} // namespace fiddle
