#include "HarmonicAnalyzer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <numeric>

namespace fiddle {
namespace hmm {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

HarmonicAnalyzer::HarmonicAnalyzer() {
  // Zero-init emission tables.
  for (auto& row : emissionBass_)
    row.fill(-1e30f);
  for (auto& row : emissionUpper_)
    row.fill(0.f);

  // Pre-allocate ring buffer.
  ringBuffer_.resize(kDefaultLag + 1);
  for (auto& step : ringBuffer_)
    step.beam.reserve(kDefaultBeamWidth);

  candidates_.reserve(kDefaultBeamWidth * kDefaultTopK);
}

// ─────────────────────────────────────────────────────────────────────────────
// Model Loading: Transition Matrix
// ─────────────────────────────────────────────────────────────────────────────

bool HarmonicAnalyzer::loadTransitionMatrix(const void* data, size_t bytes) {
  if (data == nullptr || bytes < sizeof(uint32_t))
    return false;

  const uint8_t* ptr = static_cast<const uint8_t*>(data);
  const uint8_t* end = ptr + bytes;

  // Read number of states.
  uint32_t numStates = 0;
  std::memcpy(&numStates, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  if (numStates != static_cast<uint32_t>(kNumStates))
    return false;

  transitions_.resize(kNumStates);

  for (int s = 0; s < kNumStates; ++s) {
    if (ptr + sizeof(uint16_t) > end)
      return false;

    uint16_t K = 0;
    std::memcpy(&K, ptr, sizeof(uint16_t));
    ptr += sizeof(uint16_t);

    transitions_[s].resize(K);

    size_t entryBytes = K * (sizeof(uint16_t) + sizeof(float));
    if (ptr + entryBytes > end)
      return false;

    for (int k = 0; k < K; ++k) {
      uint16_t target = 0;
      float logP = 0.f;
      std::memcpy(&target, ptr, sizeof(uint16_t));
      ptr += sizeof(uint16_t);
      std::memcpy(&logP, ptr, sizeof(float));
      ptr += sizeof(float);
      transitions_[s][k] = {target, logP};
    }
  }

  transitionsLoaded_ = true;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Model Loading: Emission Templates
// ─────────────────────────────────────────────────────────────────────────────

bool HarmonicAnalyzer::loadEmissionTemplates(const void* data, size_t bytes) {
  // Format: kNumChords × 24 floats (12 bass log-probs + 12 upper chroma weights).
  constexpr size_t kRowSize = 24;
  constexpr size_t expected = kNumChords * kRowSize * sizeof(float);
  if (data == nullptr || bytes < expected)
    return false;

  const float* ptr = static_cast<const float*>(data);

  for (int c = 0; c < kNumChords; ++c) {
    for (int pc = 0; pc < 12; ++pc)
      emissionBass_[c][pc] = ptr[c * kRowSize + pc];
    for (int pc = 0; pc < 12; ++pc)
      emissionUpper_[c][pc] = ptr[c * kRowSize + 12 + pc];
  }

  emissionsLoaded_ = true;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Model Loading: Degree Priors
//
// Binary format:
//   uint32 num_modes (2), uint32 num_degrees (12), uint32 num_qualities (9)
//   For each mode (0=major, 1=minor):
//     For each degree (0-11):
//       For each quality (0-8):
//         float32 log_prior
// ─────────────────────────────────────────────────────────────────────────────

bool HarmonicAnalyzer::loadDegreePriors(const void* data, size_t bytes) {
  constexpr size_t kHeaderSize = 3 * sizeof(uint32_t);
  constexpr size_t kNumEntries = 2 * 12 * kNumQualities;
  constexpr size_t kExpected = kHeaderSize + kNumEntries * sizeof(float);

  if (data == nullptr || bytes < kExpected)
    return false;

  const auto* headerPtr = static_cast<const uint8_t*>(data);

  // Validate header.
  uint32_t numModes = 0, numDegrees = 0, numQualities = 0;
  std::memcpy(&numModes, headerPtr, sizeof(uint32_t));
  headerPtr += sizeof(uint32_t);
  std::memcpy(&numDegrees, headerPtr, sizeof(uint32_t));
  headerPtr += sizeof(uint32_t);
  std::memcpy(&numQualities, headerPtr, sizeof(uint32_t));
  headerPtr += sizeof(uint32_t);

  if (numModes != 2 || numDegrees != 12 ||
      numQualities != static_cast<uint32_t>(kNumQualities))
    return false;

  const auto* floatPtr = reinterpret_cast<const float*>(headerPtr);
  for (int mode = 0; mode < 2; ++mode) {
    for (int deg = 0; deg < 12; ++deg) {
      for (int qual = 0; qual < kNumQualities; ++qual) {
        degreePriors_[mode][deg * kNumQualities + qual] = *floatPtr++;
      }
    }
  }

  degreePriorsLoaded_ = true;
  return true;
}

float HarmonicAnalyzer::degreePrior(int keyIdx, int chordIdx) const {
  if (!degreePriorsLoaded_)
    return 0.f;  // neutral if no priors loaded

  auto [keyRoot, keyMinor] = keyFromIndex(keyIdx);
  auto [chordRoot, quality] = chordFromIndex(chordIdx);

  int degree = (chordRoot - keyRoot + 12) % 12;
  int mode = keyMinor ? 1 : 0;

  return kDegreePriorScale *
         degreePriors_[mode][degree * kNumQualities +
                             static_cast<int>(quality)];
}

// ─────────────────────────────────────────────────────────────────────────────
// Default Emission Templates (music-theory bootstrap)
//
// For each chord type, we define:
//   - Bass log-prob: strong peak on expected root (root position assumed).
//   - Upper chroma: binary template of expected chord tones.
//
// This is sufficient for testing and provides a reasonable starting point
// even without trained data.
// ─────────────────────────────────────────────────────────────────────────────

void HarmonicAnalyzer::buildDefaultEmissionTemplates() {
  // Interval patterns (semitones above root) for each quality.
  struct ChordTemplate {
    std::vector<int> intervals;  // semitones above root
  };

  const ChordTemplate templates[kNumQualities] = {
    {{0, 4, 7}},           // Major
    {{0, 3, 7}},           // Minor
    {{0, 3, 6}},           // Diminished
    {{0, 4, 8}},           // Augmented
    {{0, 4, 7, 10}},       // Dom7
    {{0, 4, 7, 11}},       // Maj7
    {{0, 3, 7, 10}},       // Min7
    {{0, 3, 6, 9}},        // Dim7
    {{0, 3, 6, 10}},       // HalfDim7
  };

  // ── Bass weights ──────────────────────────────────────────────────────
  // Three tiers: root position > inversion (chord tone) > non-chord-tone.
  // These are log-probabilities.
  constexpr float kBassRoot      =  0.0f;   // root in bass (root position)
  constexpr float kBassChordTone = -1.0f;   // other chord tone in bass (inversion)
  constexpr float kBassMiss      = -3.0f;   // non-chord-tone in bass (unusual)

  // ── Upper chroma weights ──────────────────────────────────────────────
  // Chord tones are rewarded; non-chord tones are penalized.
  // After normalization and 3× scaling, chroma has enough weight to
  // overcome an inversion bass penalty when the match is good.
  constexpr float kChromaTone    =  1.0f;   // chord tone present
  constexpr float kChromaNon     = -0.5f;   // non-chord tone penalized

  for (int root = 0; root < 12; ++root) {
    for (int q = 0; q < kNumQualities; ++q) {
      int chordIdx = root * kNumQualities + q;
      const auto& tmpl = templates[q];

      // Bass: root gets highest, other chord tones get moderate,
      // non-chord-tones get strong penalty.
      for (int pc = 0; pc < 12; ++pc)
        emissionBass_[chordIdx][pc] = kBassMiss;
      emissionBass_[chordIdx][root] = kBassRoot;
      for (int interval : tmpl.intervals) {
        int pc = (root + interval) % 12;
        if (pc != root)
          emissionBass_[chordIdx][pc] = kBassChordTone;
      }

      // Upper chroma: chord tones get high weight, others penalized.
      for (int pc = 0; pc < 12; ++pc)
        emissionUpper_[chordIdx][pc] = kChromaNon;
      for (int interval : tmpl.intervals) {
        int pc = (root + interval) % 12;
        emissionUpper_[chordIdx][pc] = kChromaTone;
      }
    }
  }

  emissionsLoaded_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Emission Computation
// ─────────────────────────────────────────────────────────────────────────────

float HarmonicAnalyzer::computeEmission(int chordIdx, const Observation& obs) const {
  if (chordIdx < 0 || chordIdx >= kNumChords)
    return -1e30f;

  // Bass component: log P(observed bass | chord).
  // Distinguishes root position, inversions, and non-chord-tone bass.
  float bassScore = emissionBass_[chordIdx][obs.bassPitchClass];

  // Upper chroma component: dot product of observation and template.
  // Chord tones contribute +1.0, non-chord tones contribute -0.5,
  // so the score rewards matching and penalizes mismatching.
  float chromaScore = 0.f;
  const auto& tmpl = emissionUpper_[chordIdx];
  for (int pc = 0; pc < 12; ++pc) {
    chromaScore += obs.upperChroma[pc] * tmpl[pc];
  }

  // Normalize chroma score to prevent magnitude from scaling with
  // the number of active notes.
  float chromaNorm = 0.f;
  for (int pc = 0; pc < 12; ++pc)
    chromaNorm += obs.upperChroma[pc];
  if (chromaNorm > 1e-6f)
    chromaScore /= chromaNorm;

  // Scale chroma to balance against bass. Without scaling, the bass
  // component (range ~[-3, 0]) dominates the chroma component (range
  // ~[-0.5, 1.0]). With 3× scaling, a perfect chroma match (3.0) can
  // overcome an inversion bass penalty (-1.0) or a non-chord bass (-3.0).
  constexpr float kChromaScale = 3.0f;

  return bassScore + chromaScore * kChromaScale;
}

// ─────────────────────────────────────────────────────────────────────────────
// Beam Pruning
// ─────────────────────────────────────────────────────────────────────────────

void HarmonicAnalyzer::pruneBeam(std::vector<BeamEntry>& candidates, int maxBeam) {
  if (static_cast<int>(candidates.size()) <= maxBeam)
    return;

  // Partial sort: get the top maxBeam entries by logProb (descending).
  std::nth_element(
      candidates.begin(),
      candidates.begin() + maxBeam,
      candidates.end(),
      [](const BeamEntry& a, const BeamEntry& b) {
        return a.logProb > b.logProb;
      });

  candidates.resize(maxBeam);
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward Step
// ─────────────────────────────────────────────────────────────────────────────

void HarmonicAnalyzer::forwardStep(const Observation& obs) {
  // Advance ring buffer head.
  int prevHead = ringHead_;
  ringHead_ = (ringHead_ + 1) % static_cast<int>(ringBuffer_.size());

  Step& currentStep = ringBuffer_[ringHead_];
  currentStep.obs = obs;
  currentStep.timestampMs = obs.timestampMs;

  const Step& prevStep = ringBuffer_[prevHead];

  // ── First event: initialize beam with uniform prior ──────────────────────
  if (eventCount_ == 0) {
    currentStep.beam.clear();

    // Seed beam with all states (or top-B if kNumStates > beamWidth).
    // For the initial step, we use emission only (no transition).
    candidates_.clear();
    for (int s = 0; s < kNumStates; ++s) {
      auto [keyIdx, chordIdx] = stateFromIndex(s);
      // Use the FULL (unscaled) degree prior at initialization to firmly
      // establish the key. At subsequent events, transitions handle key
      // tracking and the prior is not applied.
      float rawPrior = degreePriorsLoaded_
          ? degreePriors_[keyFromIndex(keyIdx).second ? 1 : 0]
                [(chordFromIndex(chordIdx).first - keyFromIndex(keyIdx).first + 12) % 12
                 * kNumQualities + static_cast<int>(chordFromIndex(chordIdx).second)]
          : 0.f;
      float emit = computeEmission(chordIdx, obs) + rawPrior;
      // Uniform prior in log space: log(1/kNumStates).
      float prior = -std::log(static_cast<float>(kNumStates));
      candidates_.push_back({s, prior + emit, -1});
    }

    pruneBeam(candidates_, beamWidth_);

    // Filter initial beam to the best key.
    // The first chord establishes the key context. Without this, wrong-key
    // states survive (e.g., F:Fmaj alongside C:Cmaj) and exploit strong
    // same-key transitions (V→I) at event 2, causing false modulations.
    //
    // We find the best-scoring key root and keep both its major/minor modes.
    {
      // Find the best key root.
      std::array<float, 12> bestScorePerKeyRoot;
      bestScorePerKeyRoot.fill(-1e30f);
      for (const auto& c : candidates_) {
        auto [ki, ci] = stateFromIndex(c.stateIdx);
        auto [keyRoot, keyMinor] = keyFromIndex(ki);
        if (c.logProb > bestScorePerKeyRoot[keyRoot])
          bestScorePerKeyRoot[keyRoot] = c.logProb;
      }
      int bestKeyRoot = 0;
      for (int r = 1; r < 12; ++r) {
        if (bestScorePerKeyRoot[r] > bestScorePerKeyRoot[bestKeyRoot])
          bestKeyRoot = r;
      }
      // Keep only entries in the best key root (major or minor).
      candidates_.erase(
          std::remove_if(candidates_.begin(), candidates_.end(),
                         [&](const BeamEntry& c) {
                           auto [ki, ci] = stateFromIndex(c.stateIdx);
                           auto [keyRoot, keyMinor] = keyFromIndex(ki);
                           return keyRoot != bestKeyRoot;
                         }),
          candidates_.end());
    }

    currentStep.beam = candidates_;
    eventCount_++;
    return;
  }

  // ── Subsequent events: expand from previous beam ─────────────────────────
  candidates_.clear();

  // Find the best previous beam entry (used as baseline for fallback).
  float bestPrevProb = -1e30f;
  int bestPrevIdx = 0;
  for (int bi = 0; bi < static_cast<int>(prevStep.beam.size()); ++bi) {
    if (prevStep.beam[bi].logProb > bestPrevProb) {
      bestPrevProb = prevStep.beam[bi].logProb;
      bestPrevIdx = bi;
    }
  }

  if (transitionsLoaded_) {
    // ── Sparse transition expansion ──────────────────────────────────────
    // For each state in the previous beam, expand through its top-K
    // transitions from the trained matrix.
    //
    // Key-change penalty: transitions that modulate to a different key
    // get an additional log penalty. Without this, the V→I cadence
    // pattern causes every chord to be reinterpreted as the tonic of
    // a new key (e.g., F chord → I-in-F via F:Cmaj→F:Fmaj V→I
    // instead of C:Cmaj→C:Fmaj I→IV staying in C).
    constexpr float kKeyChangePenalty = -2.0f;

    for (int bi = 0; bi < static_cast<int>(prevStep.beam.size()); ++bi) {
      const auto& prev = prevStep.beam[bi];
      int srcState = prev.stateIdx;

      if (srcState < 0 || srcState >= kNumStates)
        continue;

      auto [srcKeyIdx, srcChordIdx] = stateFromIndex(srcState);

      if (srcState < static_cast<int>(transitions_.size())) {
        for (const auto& trans : transitions_[srcState]) {
          if (trans.targetState >= kNumStates)
            continue;
          auto [tKeyIdx, tChordIdx] = stateFromIndex(trans.targetState);
          float emit = computeEmission(tChordIdx, obs);
          float keyPenalty = (tKeyIdx != srcKeyIdx) ? kKeyChangePenalty : 0.f;
          float score = prev.logProb + trans.logProb + keyPenalty + emit;
          candidates_.push_back({static_cast<int>(trans.targetState), score, bi});

          // ── Quality smoothing (triad → 7th only) ────────────────
          // When the transition targets a triad, also create a
          // candidate for the natural 7th extension. This lets 7th
          // chords inherit transition strength from their parent triad
          // when the training data only saw the triad form.
          //
          // We do NOT smooth 7th→triad because triads already win on
          // emission when no 7th is sounding. Smoothing in that
          // direction creates phantom 7ths from clean triads.
          constexpr float kQualitySmoothPenalty = -1.0f;
          auto [tRoot, tQual] = chordFromIndex(tChordIdx);

          // Map triad → natural 7th extension (one-way).
          auto seventh = [](ChordQuality q) -> ChordQuality {
            switch (q) {
              case ChordQuality::Major:      return ChordQuality::Dom7;
              case ChordQuality::Minor:      return ChordQuality::Min7;
              case ChordQuality::Diminished: return ChordQuality::HalfDim7;
              default:                       return q;  // no change
            }
          };

          ChordQuality ext = seventh(tQual);
          if (ext != tQual) {
            int extChordIdx = chordIndex(tRoot, ext);
            int extState = stateIndex(tKeyIdx, extChordIdx);
            float extEmit = computeEmission(extChordIdx, obs);
            float extScore = prev.logProb + trans.logProb + kQualitySmoothPenalty
                             + keyPenalty + extEmit;
            candidates_.push_back({extState, extScore, bi});
          }
        }
      }
    }

    // ── Emission-only fallback ─────────────────────────────────────────
    // The sparse matrix only covers states seen in training data (682 of
    // 2592). If the beam reaches uncovered states, the transition expansion
    // above produces zero candidates and the beam dies.
    //
    // Fix: always add emission-only candidates for ALL states with a
    // "jump" penalty. This ensures the beam never collapses and can
    // always reach any state. The transition-based candidates (above) are
    // strongly preferred because they use real log-probs instead of the
    // uniform floor, so trained progressions still dominate.
    const float kJumpPenalty = -std::log(static_cast<float>(kNumStates));
    for (int s = 0; s < kNumStates; ++s) {
      auto [keyIdx, chordIdx] = stateFromIndex(s);
      float emit = computeEmission(chordIdx, obs);
      candidates_.push_back({s, bestPrevProb + kJumpPenalty + emit, bestPrevIdx});
    }
  } else {
    // ── No transition matrix: emission-only (independent classification) ──
    for (int s = 0; s < kNumStates; ++s) {
      auto [keyIdx, chordIdx] = stateFromIndex(s);
      float emit = computeEmission(chordIdx, obs);
      float transProb = -std::log(static_cast<float>(kNumStates));
      candidates_.push_back({s, bestPrevProb + transProb + emit, bestPrevIdx});
    }
  }

  // ── Deduplicate: keep only the best path to each state ─────────────────
  // Sort by state, then by logProb descending.
  std::sort(candidates_.begin(), candidates_.end(),
            [](const BeamEntry& a, const BeamEntry& b) {
              return a.stateIdx < b.stateIdx ||
                     (a.stateIdx == b.stateIdx && a.logProb > b.logProb);
            });

  // Remove duplicates (keep first = best per state).
  auto it = std::unique(candidates_.begin(), candidates_.end(),
                        [](const BeamEntry& a, const BeamEntry& b) {
                          return a.stateIdx == b.stateIdx;
                        });
  candidates_.erase(it, candidates_.end());

  // ── Prune to beam width ────────────────────────────────────────────────
  pruneBeam(candidates_, beamWidth_);
  currentStep.beam = candidates_;

  eventCount_++;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void HarmonicAnalyzer::observeEvent(const Observation& obs) {
  forwardStep(obs);
}

HarmonicState HarmonicAnalyzer::currentBestState() const {
  if (eventCount_ == 0)
    return {};

  const Step& current = ringBuffer_[ringHead_];
  if (current.beam.empty())
    return {};

  // Find the beam entry with the highest log-probability.
  const auto* best = &current.beam[0];
  for (const auto& entry : current.beam) {
    if (entry.logProb > best->logProb)
      best = &entry;
  }

  HarmonicState state = HarmonicState::fromStateIndex(best->stateIdx);
  state.bassPitchClass = current.obs.bassPitchClass;
  state.logProbability = best->logProb;
  return state;
}

HarmonicState HarmonicAnalyzer::commitDecision() const {
  if (eventCount_ == 0)
    return {};

  int effectiveLag = std::min(lag_, eventCount_ - 1);

  if (effectiveLag == 0)
    return currentBestState();

  // Find the best entry at the current step.
  const Step& current = ringBuffer_[ringHead_];
  if (current.beam.empty())
    return {};

  int bestIdx = 0;
  for (int i = 1; i < static_cast<int>(current.beam.size()); ++i) {
    if (current.beam[i].logProb > current.beam[bestIdx].logProb)
      bestIdx = i;
  }

  // Trace back through the ring buffer.
  int traceIdx = bestIdx;
  int ringPos  = ringHead_;
  int bufSize  = static_cast<int>(ringBuffer_.size());

  for (int step = 0; step < effectiveLag; ++step) {
    const Step& curStep = ringBuffer_[ringPos];
    if (traceIdx < 0 || traceIdx >= static_cast<int>(curStep.beam.size()))
      return currentBestState();  // backpointer broken, fall back

    traceIdx = curStep.beam[traceIdx].backpointer;
    ringPos  = (ringPos - 1 + bufSize) % bufSize;
  }

  // traceIdx now indexes into the beam at ringPos (the lagged step).
  const Step& laggedStep = ringBuffer_[ringPos];
  if (traceIdx < 0 || traceIdx >= static_cast<int>(laggedStep.beam.size()))
    return currentBestState();

  const auto& decided = laggedStep.beam[traceIdx];
  HarmonicState state = HarmonicState::fromStateIndex(decided.stateIdx);
  state.bassPitchClass = laggedStep.obs.bassPitchClass;
  state.logProbability = decided.logProb;
  return state;
}

bool HarmonicAnalyzer::shouldForceCommit(double currentTimeMs,
                                          double playbackDelayMs) const {
  if (eventCount_ == 0)
    return false;

  // The oldest uncommitted event's timestamp.
  int effectiveLag = std::min(lag_, eventCount_ - 1);
  int bufSize = static_cast<int>(ringBuffer_.size());
  int oldestPos = (ringHead_ - effectiveLag + bufSize) % bufSize;
  double oldestTime = ringBuffer_[oldestPos].timestampMs;

  // If the oldest uncommitted event is older than the playback delay allows,
  // we must commit now even if we haven't accumulated enough events.
  return (currentTimeMs - oldestTime) >= playbackDelayMs;
}

void HarmonicAnalyzer::reset() {
  eventCount_ = 0;
  ringHead_ = 0;

  // Resize ring buffer if lag changed.
  int newSize = lag_ + 1;
  if (static_cast<int>(ringBuffer_.size()) != newSize)
    ringBuffer_.resize(newSize);

  for (auto& step : ringBuffer_) {
    step.beam.clear();
    step.timestampMs = 0.0;
  }
}

} // namespace hmm
} // namespace fiddle
