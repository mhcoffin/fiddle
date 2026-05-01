#pragma once

#include "AnnotationRecord.h"
#include "TonalContext.h"
#include "midi_event.pb.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fiddle {

/// Read-only context passed to Annotator methods.
struct AnnotatorContext {
  int stripChannel = 1; // 1-based output channel of the strip
  double sampleRate = 44100.0;
  double currentTimeMs = 0.0;

  /// Currently active notes across all strips (read-only snapshot).
  const std::vector<fiddle::Note> *activeNotes = nullptr;

  /// Current CC state: currentCCs[channel_0based][ccNumber] → value (0-127).
  const std::array<std::array<uint8_t, 128>, 16> *currentCCs = nullptr;

  /// Tonal context computed by TonalCenterClassifier for the current note.
  /// key-level fields are populated first, then annotateNote() fills
  /// is_diatonic and scale_degree before the annotator chain runs.
  TonalContext tonalContext;
};

/**
 * Abstract Annotator interface.
 *
 * An Annotator translates a Note's semantic annotations (articulation,
 * technique, dynamics mode) into concrete MIDI instructions that drive
 * a specific VST. Two implementations are planned:
 *   - ExpressionMapAnnotator (Phase 2): uses ExpressionMapData
 *   - LuaAnnotator (Phase 3): runs user-supplied Lua scripts
 *
 * Each MixerStrip owns one Annotator instance, so annotators may hold
 * per-strip state (e.g., current keyswitch, round-robin counter).
 */
class Annotator {
public:
  virtual ~Annotator() = default;

  /// Called before the noteOn is routed to the VST.
  /// Populate note.pre_note() with setup MIDI (keyswitches, CCs, etc.).
  /// May also modify note fields (velocity, channel).
  virtual void onNoteStart(fiddle::Note &note,
                           const AnnotatorContext &ctx) = 0;

  /// Called after noteOff.
  /// Populate note.post_note() with cleanup MIDI if needed.
  virtual void onNoteEnd(fiddle::Note &note, const AnnotatorContext &ctx) = 0;

  /// Called when a CC event arrives on this strip's input.
  /// Return true to forward the CC to the VST as-is.
  /// Return false to suppress it (annotator consumed/transformed it).
  /// The annotator may also schedule replacement MIDI via the note's
  /// pre_note/post_note, or via a side-channel in a future phase.
  virtual bool onCC(const fiddle::MidiEvent &event,
                    const AnnotatorContext &ctx) {
    (void)event;
    (void)ctx;
    return true; // default: forward
  }

  /// Human-readable name for UI display.
  virtual std::string name() const = 0;

  /// Timing delta (ms) computed by the last onNoteEnd() call.
  /// Negative = early noteOff, positive = late noteOff (overlap).
  virtual double durationAdjustMs() const { return 0.0; }

  /// If > 0, schedules an early note-off at note-on time + this many ms.
  /// This is set during onNoteStart and checked by routeAnnotatedNoteOn.
  /// Unlike durationAdjustMs (which adjusts Dorico's note-off timing),
  /// this proactively schedules a note-off independent of Dorico.
  virtual double scheduledNoteOffMs() const { return -1.0; }

  /// Reset cached state (match cache, current match, etc.).
  /// Called when the user clears the Note Inspector so subsequent
  /// notes are resolved fresh.
  virtual void resetState() {}

  /// Access the most recent annotation decision record.
  /// Returns nullptr for annotators that don't produce records.
  virtual const AnnotationRecord *lastAnnotationRecord() const {
    return nullptr;
  }
};

/**
 * No-op annotator that preserves current behavior: all MIDI passes through
 * unchanged. This is the default when no expression map or Lua script is
 * assigned to a strip.
 */
class PassthroughAnnotator : public Annotator {
public:
  void onNoteStart(fiddle::Note &, const AnnotatorContext &) override {}
  void onNoteEnd(fiddle::Note &, const AnnotatorContext &) override {}
  bool onCC(const fiddle::MidiEvent &, const AnnotatorContext &) override {
    return true;
  }
  std::string name() const override { return "Passthrough"; }
};

} // namespace fiddle
