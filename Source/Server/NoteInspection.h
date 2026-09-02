#pragma once

#include "AnnotationRecord.h"
#include "midi_event.pb.h"

#include <set>
#include <string>

namespace fiddle {

/// Convert Dorico's display technique to the pt.xxx identifier used by maps.
std::string techniqueIdFromDisplay(const std::string &display);

/// Collect non-default playback techniques from an incoming structured note.
std::set<std::string> collectInputTechniqueIds(const fiddle::Note &note);

/// Determine the note-length category supplied by Dorico, falling back to the
/// current duration when a category was not supplied.
LengthCategory inputLengthCategory(const fiddle::Note &note,
                                   double sampleRate);

/// Capture the note exactly as it entered the strip's annotation pipeline.
AnnotationRecord makeIncomingNoteRecord(const fiddle::Note &note,
                                        double sampleRate,
                                        bool expressionMapAssigned);

/// Enrich an input snapshot with an optional expression-map decision while
/// retaining the original note, velocity, techniques, and dimensions.
AnnotationRecord mergeNoteInspection(const AnnotationRecord &input,
                                     const AnnotationRecord *decision);

} // namespace fiddle
