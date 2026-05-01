#include "TonalCenterClassifier.h"

#include <algorithm>
#include <cmath>

namespace fiddle {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

TonalCenterClassifier::TonalCenterClassifier(std::unique_ptr<IKeyProfile> profile)
    : profile_(std::move(profile)) {
  histogram_.fill(0.f);
  buildTemplates();
}

TonalCenterClassifier::~TonalCenterClassifier() {
  stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void TonalCenterClassifier::start() {
  startTimer(50); // 50ms interval
}

void TonalCenterClassifier::stop() {
  stopTimer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Template pre-computation (Pearson normalization)
//
// Each template is the raw profile vector (12 floats) shifted to zero-mean
// and scaled to unit L2-norm. This makes the inner product between the
// normalized histogram and a normalized template equal to Pearson r directly,
// avoiding redundant division on every correlation call.
// ─────────────────────────────────────────────────────────────────────────────

void TonalCenterClassifier::buildTemplates() {
  templates_.clear();
  templates_.reserve(24);

  for (int root = 0; root < 12; ++root) {
    for (int mode = 0; mode < 2; ++mode) {
      bool minor = (mode == 1);
      Template t;
      t.root  = root;
      t.minor = minor;

      // Sample raw weights for all 12 pitch classes.
      for (int pc = 0; pc < 12; ++pc)
        t.weights[pc] = profile_->weight(root, minor, pc);

      // Zero-mean.
      float mean = 0.f;
      for (float w : t.weights) mean += w;
      mean /= 12.f;
      for (float &w : t.weights) w -= mean;

      // Unit L2 norm.
      float norm = 0.f;
      for (float w : t.weights) norm += w * w;
      norm = std::sqrt(norm);
      if (norm > 1e-6f) {
        for (float &w : t.weights) w /= norm;
      }

      templates_.push_back(t);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Note feed
// ─────────────────────────────────────────────────────────────────────────────

void TonalCenterClassifier::enqueueNote(int pitchClass, float durationBeats,
                                        double scheduledTimeMs) {
  if (pitchClass < 0 || pitchClass > 11 || durationBeats <= 0.f)
    return;

  // Weight = duration in beats (already computed by caller using live BPM).
  // We add directly to the EWMA histogram here. The timer will apply
  // exponential decay on the next tick relative to lastUpdateTimeMs_.
  std::lock_guard<std::mutex> lock(bufferMutex_);

  // Apply decay since last update before adding the new note.
  double nowMs = juce::Time::getMillisecondCounterHiRes();
  if (lastUpdateTimeMs_ > 0.0 && decayTauMs_ > 0.0) {
    double dt = nowMs - lastUpdateTimeMs_;
    float decay = static_cast<float>(std::exp(-dt / decayTauMs_));
    for (float &h : histogram_) h *= decay;
  }
  lastUpdateTimeMs_ = nowMs;

  histogram_[pitchClass] += durationBeats;
}

void TonalCenterClassifier::removeNote(int pitchClass, float durationBeats) {
  if (pitchClass < 0 || pitchClass > 11 || durationBeats <= 0.f)
    return;

  std::lock_guard<std::mutex> lock(bufferMutex_);
  histogram_[pitchClass] = std::max(0.f, histogram_[pitchClass] - durationBeats);
}

// ─────────────────────────────────────────────────────────────────────────────
// Timer callback (~50ms tick)
// ─────────────────────────────────────────────────────────────────────────────

void TonalCenterClassifier::timerCallback() {
  auto histogram = snapshotHistogram();

  // Skip correlation if the histogram is essentially empty (transport stopped).
  float total = 0.f;
  for (float h : histogram) total += h;
  if (total < 0.01f)
    return;

  KeyCandidate winner = runCorrelation(histogram);
  updateHysteresis(winner);
}

std::array<float, 12> TonalCenterClassifier::snapshotHistogram() {
  std::lock_guard<std::mutex> lock(bufferMutex_);

  // Apply decay for time elapsed since last note was enqueued.
  double nowMs = juce::Time::getMillisecondCounterHiRes();
  if (lastUpdateTimeMs_ > 0.0 && decayTauMs_ > 0.0) {
    double dt = nowMs - lastUpdateTimeMs_;
    float decay = static_cast<float>(std::exp(-dt / decayTauMs_));
    for (float &h : histogram_) h *= decay;
  }
  lastUpdateTimeMs_ = nowMs;

  return histogram_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pearson correlation engine
//
// The histogram is first zero-meaned and unit-normed (matching the template
// pre-normalization), then the inner product with each template gives r.
// ─────────────────────────────────────────────────────────────────────────────

TonalCenterClassifier::KeyCandidate
TonalCenterClassifier::runCorrelation(const std::array<float, 12> &histogram) const {
  // Normalize the histogram to match template normalization.
  std::array<float, 12> h = histogram;

  float mean = 0.f;
  for (float v : h) mean += v;
  mean /= 12.f;
  for (float &v : h) v -= mean;

  float norm = 0.f;
  for (float v : h) norm += v * v;
  norm = std::sqrt(norm);

  KeyCandidate best{0, false, -2.f};

  if (norm < 1e-6f) {
    // Degenerate histogram (all pitch classes equal) — no information.
    return best;
  }

  for (float &v : h) v /= norm;

  for (const auto &tmpl : templates_) {
    float r = 0.f;
    for (int pc = 0; pc < 12; ++pc)
      r += h[pc] * tmpl.weights[pc];

    if (r > best.r) {
      best.root  = tmpl.root;
      best.minor = tmpl.minor;
      best.r     = r;
    }
  }

  return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hysteresis manager
//
// Prevents flickering between closely-scored keys. A new key becomes the
// current key only when it has been the winner continuously for hysteresisMs_,
// OR when its confidence exceeds the current key's confidence by at least
// hysteresisMargin_.
// ─────────────────────────────────────────────────────────────────────────────

void TonalCenterClassifier::updateHysteresis(KeyCandidate winner) {
  double nowMs = juce::Time::getMillisecondCounterHiRes();

  bool winnerMatchesCurrent = (winner.root  == currentWinner_.root &&
                                winner.minor == currentWinner_.minor);
  if (winnerMatchesCurrent) {
    // Current key still winning — update confidence and commit.
    currentWinner_.r = winner.r;
    pendingFirstSeenMs_ = -1.0;

    TonalContext ctx;
    ctx.current_key_root = currentWinner_.root;
    ctx.is_minor         = currentWinner_.minor;
    ctx.confidence       = currentWinner_.r;
    cached_.store(ctx);
    return;
  }

  // Different key is winning. Check hysteresis.
  bool sameAsPending = (winner.root  == pendingWinner_.root &&
                         winner.minor == pendingWinner_.minor);

  if (!sameAsPending) {
    // New challenger.
    pendingWinner_      = winner;
    pendingFirstSeenMs_ = nowMs;
    return; // Wait for it to hold.
  }

  // Pending key is still leading — check margin or duration.
  bool marginWin    = (winner.r - currentWinner_.r) >= hysteresisMargin_;
  bool durationWin  = (pendingFirstSeenMs_ > 0.0 &&
                       (nowMs - pendingFirstSeenMs_) >= hysteresisMs_);

  if (marginWin || durationWin) {
    // Commit the new key.
    currentWinner_      = winner;
    pendingFirstSeenMs_ = -1.0;

    TonalContext ctx;
    ctx.current_key_root = currentWinner_.root;
    ctx.is_minor         = currentWinner_.minor;
    ctx.confidence       = currentWinner_.r;
    cached_.store(ctx);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-note annotation (lock-free, called from routeAnnotatedNoteOn)
//
// Fills is_diatonic and scale_degree for a specific MIDI note number given
// the current key context. Called once per note, on the message/routing thread.
// ─────────────────────────────────────────────────────────────────────────────

// static
TonalContext TonalCenterClassifier::annotateNote(TonalContext key, int noteNumber) {
  int pc = noteNumber % 12;
  int degree = ((pc - key.current_key_root) % 12 + 12) % 12;

  // Diatonic scale degrees (0-based semitones from tonic) for major and minor.
  // Major: W W H W W W H  → 0 2 4 5 7 9 11
  // Natural minor: W H W W H W W → 0 2 3 5 7 8 10
  static constexpr int kMajorDegrees[7] = {0, 2, 4, 5, 7, 9, 11};
  static constexpr int kMinorDegrees[7] = {0, 2, 3, 5, 7, 8, 10};

  const int *degrees = key.is_minor ? kMinorDegrees : kMajorDegrees;

  key.is_diatonic  = false;
  key.scale_degree = 0;

  for (int i = 0; i < 7; ++i) {
    if (degrees[i] == degree) {
      key.is_diatonic  = true;
      key.scale_degree = i + 1; // 1-based (1=tonic … 7=leading tone)
      break;
    }
  }

  return key;
}

} // namespace fiddle
