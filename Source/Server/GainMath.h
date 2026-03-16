#pragma once

#include <cmath>
#include <vector>

namespace fiddle {

/// Pure-function utility for power-model gain calculations.
/// All dB values use the convention: gain = 10^(dB/20).
/// No JUCE dependency — can be tested standalone.
struct GainMath {
  /// dB value treated as silence (matches JUCE default).
  static constexpr float kMinusInfinityDb = -100.0f;

  /// Convert dB to linear gain. Values <= kMinusInfinityDb return 0.
  static float gainFromDb(float db) {
    if (db <= kMinusInfinityDb)
      return 0.0f;
    return std::pow(10.0f, db * 0.05f); // 10^(db/20)
  }

  /// Convert linear gain to dB. Gain <= 0 returns kMinusInfinityDb.
  static float dbFromGain(float gain) {
    if (gain <= 0.0f)
      return kMinusInfinityDb;
    return 20.0f * std::log10(gain);
  }

  /// Power of a signal = gain^2.
  static float power(float gain) { return gain * gain; }

  /// Power from a dB value.
  static float powerFromDb(float db) {
    float g = gainFromDb(db);
    return g * g;
  }

  // ----- Strip descriptor for group operations -----

  struct Strip {
    float db = 0.0f;
    bool muted = false;
  };

  /// Total power of a group (sum of unmuted strip powers).
  static float groupPower(const std::vector<Strip> &strips) {
    float total = 0.0f;
    for (auto &s : strips) {
      if (!s.muted)
        total += powerFromDb(s.db);
    }
    return total;
  }

  /// Group gain = sqrt(group power).
  static float groupGain(const std::vector<Strip> &strips) {
    return std::sqrt(groupPower(strips));
  }

  /// Group fader position in dB.
  static float groupDb(const std::vector<Strip> &strips) {
    return dbFromGain(groupGain(strips));
  }

  // ----- Locked-group operations -----

  /// After one strip's dB changed in a locked group, redistribute power
  /// among the *other* unmuted strips to keep group power constant.
  ///
  /// @param strips          All strips (with the changed strip already at its
  ///                        new dB value).
  /// @param changedIndex    Index of the strip that the user moved.
  /// @param oldGroupPower   Group power before the change.
  ///
  /// @return Updated strip dBs. If redistribution is impossible (no other
  ///         unmuted strips to absorb), all strips are returned as-is
  ///         (the group fader will have to move instead).
  static std::vector<Strip>
  redistributeForLockedGroup(const std::vector<Strip> &strips,
                             int changedIndex, float oldGroupPower) {
    auto result = strips;

    // Power of the changed strip (post-change)
    float changedPower =
        result[changedIndex].muted ? 0.0f
                                   : powerFromDb(result[changedIndex].db);

    // How much power the other unmuted strips must collectively provide
    float requiredOtherPower = oldGroupPower - changedPower;
    if (requiredOtherPower < 0.0f)
      requiredOtherPower = 0.0f;

    // Current total power of other unmuted strips
    float currentOtherPower = 0.0f;
    for (int i = 0; i < (int)result.size(); ++i) {
      if (i != changedIndex && !result[i].muted)
        currentOtherPower += powerFromDb(result[i].db);
    }

    // If no other unmuted strips can absorb, signal the caller
    // by returning strips unchanged — group fader must track.
    if (currentOtherPower <= 0.0f)
      return result;

    float scale = requiredOtherPower / currentOtherPower;

    for (int i = 0; i < (int)result.size(); ++i) {
      if (i != changedIndex && !result[i].muted) {
        float p = powerFromDb(result[i].db) * scale;
        float g = std::sqrt(p);
        result[i].db = dbFromGain(g);
      }
    }

    return result;
  }

  /// When the locked group fader itself is moved, scale all unmuted strip
  /// powers by newGroupPower / oldGroupPower.
  ///
  /// @param strips          Current strips.
  /// @param oldGroupPower   Group power before the fader move.
  /// @param newGroupDb      New group fader position in dB.
  ///
  /// @return Updated strip dBs.
  static std::vector<Strip>
  scaleAllStrips(const std::vector<Strip> &strips, float oldGroupPower,
                 float newGroupDb) {
    auto result = strips;

    float newGroupGain = gainFromDb(newGroupDb);
    float newGroupPower = newGroupGain * newGroupGain;

    if (oldGroupPower <= 0.0f) {
      // All were silent — nothing to scale
      return result;
    }

    float scale = newGroupPower / oldGroupPower;

    for (auto &s : result) {
      if (!s.muted) {
        float p = powerFromDb(s.db) * scale;
        float g = std::sqrt(p);
        s.db = dbFromGain(g);
      }
    }

    return result;
  }
};

} // namespace fiddle
