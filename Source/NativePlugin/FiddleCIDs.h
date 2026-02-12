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

static const Steinberg::FUID kFiddleProcessorUID(0xABCDEF01, 0x9182FAEB,
                                                 0x4D616E75, 0x4669646C);

static const Steinberg::FUID kFiddleControllerUID(0xABCDEF01, 0x1234ABCD,
                                                  0x4D616E75, 0x4669646C);

} // namespace fiddle
