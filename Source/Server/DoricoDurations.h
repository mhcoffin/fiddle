#pragma once

#include <array>
#include <set>
#include <string>

namespace fiddle {

/// Dorico's note-duration playback options (Library ▸ Playback Options ▸
/// Timing), which an expression map may override per category.
enum class DurationCategory { Note, Staccato, Staccatissimo, Legato, Tenuto, Marcato };
inline constexpr size_t kDurationCategoryCount = 6;

/// Percent of written length per DurationCategory.
using DurationPercents = std::array<int, kDurationCategoryCount>;
inline constexpr DurationPercents kDoricoDefaultDurationPercents{90, 50, 25, 105, 100, 75};
inline constexpr int kMinDurationPercent = 1, kMaxDurationPercent = 200;

/// The expression-map option name for each category, in enum order.
inline const std::array<const char *, kDurationCategoryCount> &durationOptionNames() {
  static const std::array<const char *, kDurationCategoryCount> names{
      "timingOptions.noteDurationPercent", "timingOptions.staccatoDurationPercent",
      "timingOptions.staccatissimoDurationPercent", "timingOptions.legatoDurationPercent",
      "timingOptions.tenutoDurationPercent", "timingOptions.marcatoDurationPercent"};
  return names;
}

inline const std::array<const char *, kDurationCategoryCount> &durationCategoryLabels() {
  static const std::array<const char *, kDurationCategoryCount> labels{
      "Notes", "Staccato", "Staccatissimo", "Legato", "Tenuto", "Marcato"};
  return labels;
}

/// The category whose duration applies to a note's techniques. Priority:
/// staccatissimo > staccato > legato > tenuto > marcato > other notes.
inline DurationCategory durationCategory(const std::set<std::string> &techniques) {
  if (techniques.count("pt.staccatissimo")) return DurationCategory::Staccatissimo;
  if (techniques.count("pt.staccato")) return DurationCategory::Staccato;
  if (techniques.count("pt.legato")) return DurationCategory::Legato;
  if (techniques.count("pt.tenuto")) return DurationCategory::Tenuto;
  if (techniques.count("pt.marcato")) return DurationCategory::Marcato;
  return DurationCategory::Note;
}

/// Dorico's "Shorten note if followed by abutting note of same pitch".
/// Bridge maps pin it off; Fiddle ends same-pitch notes per layer instead.
inline constexpr const char *kTruncateSamePitchOption =
    "timingOptions.alwaysTruncateAbuttingNotesOfSamePitch";

/// Note dimension set on notes whose bridge map pins every duration option
/// to 100%: the received duration is then the written length.
inline constexpr const char *kWrittenDurationDimension = "dorico_written_duration";

} // namespace fiddle
