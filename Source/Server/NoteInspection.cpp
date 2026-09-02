#include "NoteInspection.h"

#include <cctype>

namespace fiddle {

std::string techniqueIdFromDisplay(const std::string &display) {
  if (display.empty())
    return {};
  if (display.size() > 3 && display[0] == 'p' && display[1] == 't' &&
      display[2] == '.')
    return display;

  std::string id = "pt.";
  for (const char c : display) {
    if (c == ' ' || c == '-' || c == '_')
      continue;
    id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return id;
}

std::set<std::string> collectInputTechniqueIds(const fiddle::Note &note) {
  std::set<std::string> result;
  for (const auto &[dimension, display] : note.notation_techniques()) {
    const auto defaultIt = note.notation_is_default().find(dimension);
    if (defaultIt != note.notation_is_default().end() && defaultIt->second)
      continue;

    auto id = techniqueIdFromDisplay(display);
    if (!id.empty())
      result.insert(std::move(id));
  }
  return result;
}

LengthCategory inputLengthCategory(const fiddle::Note &note,
                                   double sampleRate) {
  const auto dimension =
      note.notation_dimensions().find("dorico_length_category");
  if (dimension != note.notation_dimensions().end()) {
    switch (static_cast<int>(dimension->second)) {
    case 0:
      return LengthCategory::VeryShort;
    case 1:
      return LengthCategory::Short;
    case 2:
      return LengthCategory::Medium;
    case 3:
      return LengthCategory::Long;
    default:
      return LengthCategory::VeryLong;
    }
  }

  if (note.duration_samples() > 0 && sampleRate > 0) {
    const auto durationSeconds =
        static_cast<double>(note.duration_samples()) / sampleRate;
    return classifyLength(durationSeconds);
  }
  return LengthCategory::VeryLong;
}

AnnotationRecord makeIncomingNoteRecord(const fiddle::Note &note,
                                        double sampleRate,
                                        bool expressionMapAssigned) {
  AnnotationRecord record;
  record.noteNumber = static_cast<int>(note.note_number());
  record.inputChannel = static_cast<int>(note.channel());
  record.inputTechniques = collectInputTechniqueIds(note);
  record.lengthCategory = inputLengthCategory(note, sampleRate);
  record.velocityBefore = static_cast<int>(note.start_velocity());
  record.velocityAfter = record.velocityBefore;
  record.expressionMapAssigned = expressionMapAssigned;

  for (const auto &[dimension, display] : note.notation_techniques())
    record.receivedTechniques.emplace(dimension, display);
  for (const auto &[dimension, value] : note.notation_dimensions())
    record.receivedDimensions.emplace(dimension, value);
  for (const auto &[dimension, isDefault] : note.notation_is_default()) {
    if (isDefault)
      record.defaultTechniqueDimensions.insert(dimension);
  }

  return record;
}

AnnotationRecord mergeNoteInspection(const AnnotationRecord &input,
                                     const AnnotationRecord *decision) {
  if (decision == nullptr)
    return input;

  auto merged = *decision;
  merged.noteNumber = input.noteNumber;
  merged.inputChannel = input.inputChannel;
  merged.inputTechniques = input.inputTechniques;
  merged.receivedTechniques = input.receivedTechniques;
  merged.receivedDimensions = input.receivedDimensions;
  merged.defaultTechniqueDimensions = input.defaultTechniqueDimensions;
  merged.expressionMapAssigned = input.expressionMapAssigned;
  merged.lengthCategory = input.lengthCategory;
  merged.velocityBefore = input.velocityBefore;
  return merged;
}

} // namespace fiddle
