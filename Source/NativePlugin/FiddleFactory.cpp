#include "FiddleCIDs.h"
#include "FiddleController.h"
#include "FiddleProcessor.h"
#include "FiddleVersion.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

//----------------------------------------------------------------------
// Required by the VST3 SDK's macmain.cpp entry point
//----------------------------------------------------------------------
bool InitModule() { return true; }
bool DeinitModule() { return true; }

//----------------------------------------------------------------------
// Plugin factory registration
//----------------------------------------------------------------------

BEGIN_FACTORY_DEF(FIDDLE_VENDOR, FIDDLE_URL, FIDDLE_EMAIL)

//--- Processor (Audio Component) ---
DEF_CLASS2(INLINE_UID_FROM_FUID(fiddle::kFiddleProcessorUID),
           PClassInfo::kManyInstances, kVstAudioEffectClass, FIDDLE_NAME,
           kSimpleModeSupported, PlugType::kInstrumentSynth,
           FIDDLE_ORIGINAL_VERSION_STR, kVstVersionString,
           fiddle::FiddleProcessor::createInstance)

//--- Controller ---
DEF_CLASS2(INLINE_UID_FROM_FUID(fiddle::kFiddleControllerUID),
           PClassInfo::kManyInstances, kVstComponentControllerClass,
           FIDDLE_NAME " Controller", 0, "", FIDDLE_ORIGINAL_VERSION_STR,
           kVstVersionString, fiddle::FiddleController::createInstance)

END_FACTORY
