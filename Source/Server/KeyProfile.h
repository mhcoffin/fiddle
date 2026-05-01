#pragma once

namespace fiddle {

// ─────────────────────────────────────────────────────────────────────────────
// IKeyProfile
//
// Abstract interface for a pitch-class weighting profile used by the
// Krumhansl-Schmuckler tonal center algorithm.
//
// Implementations must return the relative weight of each pitch class pc
// (0=C, 1=C#, …, 11=B) given the hypothetical key root and mode.
// ─────────────────────────────────────────────────────────────────────────────
struct IKeyProfile {
  virtual ~IKeyProfile() = default;

  /// Weight of pitch class `pc` (0–11) in the key rooted at `root` (0–11),
  /// major or minor.
  virtual float weight(int root, bool minor, int pc) const = 0;

  /// Human-readable name (e.g. "Temperley").
  virtual const char *name() const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// TemperleyProfile
//
// Temperley's European Classical weighting vectors, which give better
// discrimination than the original Krumhansl-Kessler profiles for
// common-practice music (Bach, etc.).
//
// Source: Temperley, D. (2007). Music and Probability.
// ─────────────────────────────────────────────────────────────────────────────
class TemperleyProfile : public IKeyProfile {
public:
  float weight(int root, bool minor, int pc) const override {
    int degree = ((pc - root) % 12 + 12) % 12;
    return minor ? kMinor[degree] : kMajor[degree];
  }

  const char *name() const override { return "Temperley"; }

private:
  // Weights indexed by scale degree (0=tonic … 11=major 7th).
  // Major: {tonic, m2, M2, m3, M3, P4, tritone, P5, m6, M6, m7, M7}
  static constexpr float kMajor[12] = {5.0f, 2.0f, 3.5f, 2.0f, 4.5f, 4.0f,
                                        2.0f, 4.5f, 2.0f, 3.5f, 1.5f, 4.0f};
  // Minor: same order but m3 ↔ M3 and m6 ↔ M6 swapped.
  static constexpr float kMinor[12] = {5.0f, 2.0f, 3.5f, 4.5f, 2.0f, 4.0f,
                                        2.0f, 4.5f, 3.5f, 2.0f, 1.5f, 4.0f};
};

} // namespace fiddle
