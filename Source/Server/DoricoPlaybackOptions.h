#pragma once

#include <algorithm>
#include <array>
#include <cmath>
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

/// Dorico's "Expression map switches before notes" (ticks, 480 per quarter
/// note assumed). Bridge maps pin it to Dorico's default so the decoder sees
/// fixed timing; layers apply the project's value (or their map's override).
inline constexpr const char *kSwitchLeadOption = "timingOptions.numTicksBeforeForSwitches";
inline constexpr int kDoricoDefaultSwitchLeadTicks = 10;
inline constexpr int kMaxSwitchLeadTicks = 1920;

/// Dorico's "Shorten note if followed by abutting note of same pitch".
/// Bridge maps pin it off; Fiddle ends same-pitch notes per layer instead.
inline constexpr const char *kTruncateSamePitchOption =
    "timingOptions.alwaysTruncateAbuttingNotesOfSamePitch";

/// Dorico's dynamics playback options (experiments/dynamics-curve/results-01.md).
/// A dynamic level (p -1, pp -2 … mp -0.5, mf +0.5, f 1 …) is placed between
/// the minimum and maximum level, bent by an S-curve around `midpoint`, and
/// sent as round(1 + 126 * curve) on CC1 or velocity.
struct DoricoDynamics {
  double midpoint = 0.5, lowerPower = 2.5, upperPower = 2.5;
  int minLevel = -6, maxLevel = 6;
  // Level added to accented, marcato, tenuto notes; subtracted for ghost notes.
  double accent = 0.5, marcato = 0.8, tenuto = 0.0, ghost = 2.5;

  bool operator==(const DoricoDynamics &o) const {
    return midpoint == o.midpoint && lowerPower == o.lowerPower && upperPower == o.upperPower &&
           minLevel == o.minLevel && maxLevel == o.maxLevel && accent == o.accent &&
           marcato == o.marcato && tenuto == o.tenuto && ghost == o.ghost;
  }
  bool operator!=(const DoricoDynamics &o) const { return !(*this == o); }

  /// The 7-bit value (1..127) Dorico sends for a level.
  int midiValue(double level) const {
    const double span = std::max(1, maxLevel - minLevel);
    const double x = std::clamp((level - minLevel) / span, 0.0, 1.0);
    const double m = std::clamp(midpoint, 0.01, 0.99);
    const double y = x < m ? m * std::pow(x / m, lowerPower)
                           : 1.0 - (1.0 - m) * std::pow((1.0 - x) / (1.0 - m), upperPower);
    return std::clamp(static_cast<int>(std::floor(1.0 + 126.0 * y + 0.5)), 1, 127);
  }

  /// Level added for the articulations among `techniques`.
  double articulationDelta(const std::set<std::string> &techniques) const {
    double delta = 0.0;
    if (techniques.count("pt.accent")) delta += accent;
    if (techniques.count("pt.marcato")) delta += marcato;
    if (techniques.count("pt.tenuto")) delta += tenuto;
    if (techniques.count("pt.ghostNote")) delta -= ghost;
    return delta;
  }
};

/// Bridge maps pin a linear curve over this range and no articulation deltas,
/// so a received value maps back to its level exactly (one 7-bit step is
/// 1/7.9 level). The range is wider than ffffff..pppppp so the effects left
/// to Dorico (beat stress, humanisation, contour emphasis) are not clipped.
inline constexpr int kBridgeDynamicsMinLevel = -8, kBridgeDynamicsMaxLevel = 8;

/// The level a pinned bridge's CC1 or velocity value stands for.
inline double bridgeDynamicLevel(int value) {
  return kBridgeDynamicsMinLevel + (kBridgeDynamicsMaxLevel - kBridgeDynamicsMinLevel) *
                                       (std::clamp(value, 1, 127) - 1) / 126.0;
}

/// Map override and option names Dorico uses for these settings.
inline constexpr const char *kDynamicCurveOption = "dynamicsOptions.dynamicCurveParams";
inline constexpr const char *kMinDynamicLevelOption = "dynamicsOptions.minProfileLevelForPlayback";
inline constexpr const char *kMaxDynamicLevelOption = "dynamicsOptions.maxProfileLevelForPlayback";
inline constexpr const char *kAccentDeltaOption = "dynamicsOptions.accentDynamicDelta";
inline constexpr const char *kMarcatoDeltaOption = "dynamicsOptions.marcatoDynamicDelta";
inline constexpr const char *kTenutoDeltaOption = "dynamicsOptions.tenutoDynamicDelta";
inline constexpr const char *kGhostDeltaOption = "dynamicsOptions.ghostNoteDynamicDelta";

/// The project's copy of the Dorico playback options Fiddle applies itself.
struct DoricoPlaybackSettings {
  DurationPercents durations = kDoricoDefaultDurationPercents;
  int switchLeadTicks = kDoricoDefaultSwitchLeadTicks;
  DoricoDynamics dynamics;
};

/// Note dimension set on notes whose bridge map pins Dorico's playback
/// options: durations arrive at written length, switches at a fixed lead,
/// dynamics on a linear curve (kBridgeDynamics*Level) without articulation
/// deltas.
inline constexpr const char *kPinnedPlaybackDimension = "dorico_pinned_playback";

} // namespace fiddle
