#pragma once

#include "DoricoPlaybackOptions.h"
#include "NoteStartSettler.h"
#include "midi_event.pb.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fiddle::scoretest {

// Score tests (tests/score): while a test is armed, record every note the
// bridge decoder delivers, then compare them with the test's expected result
// (generated/<id>.expected.json). The comparison sees exactly what layers
// would receive, before any annotator or instrument.

using Endpoint = std::pair<int, int>; // zero-based port, 1-based channel

inline juce::String pitchName(int pitch) {
  static const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return juce::String(names[((pitch % 12) + 12) % 12]) + juce::String(pitch / 12 - 1);
}

inline double rounded(double value, double step = 0.001) { return std::round(value / step) * step; }

struct ReceivedNote {
  uint64_t id = 0;
  Endpoint endpoint{0, 1};
  int pitch = 0, velocity = 0;
  double onsetMs = 0;               // host clock
  std::optional<double> durationMs; // unset: the note never ended
  bool ccDynamics = false, pinned = false;
  std::vector<std::string> techniques;    // sorted, unique
  std::vector<std::pair<double, int>> cc1; // ms after onset, value

  static ReceivedNote from(const Note &note, double sampleRate, bool ended) {
    const double rate = sampleRate > 0 ? sampleRate : 44100.0;
    ReceivedNote r;
    r.id = note.id();
    r.endpoint = {int(note.port()), int(note.channel())};
    r.pitch = int(note.note_number());
    r.velocity = int(note.start_velocity());
    r.onsetMs = double(note.start_sample()) * 1000.0 / rate;
    if (ended) r.durationMs = double(note.duration_samples()) * 1000.0 / rate;
    r.ccDynamics = note.dynamics_mode() == Note::CC;
    r.pinned = note.notation_dimensions().count(kPinnedPlaybackDimension) > 0;
    std::set<std::string> techniques;
    for (const auto &[key, id] : note.notation_techniques()) techniques.insert(id);
    r.techniques.assign(techniques.begin(), techniques.end());
    if (const auto lane = note.cc_automation().find(1); lane != note.cc_automation().end())
      for (const auto &point : lane->second.points())
        r.cc1.emplace_back(double(point.offset_samples()) * 1000.0 / rate, int(point.value()));
    return r;
  }

  // The value that sets the note's starting dynamic: velocity, or the CC1
  // in force once Dorico's late CC1 has arrived (as NoteStartSettler waits).
  std::optional<int> startValue() const {
    if (!ccDynamics) return velocity;
    std::optional<int> value;
    for (const auto &[ms, v] : cc1)
      if (ms <= NoteStartSettler::kSettleMs) value = v;
    return value;
  }
  // Dorico's dynamic level, read through the bridge's pinned linear curve.
  std::optional<double> startLevel() const {
    const auto value = startValue();
    if (!value) return std::nullopt;
    return bridgeDynamicLevel(*value);
  }
  // CC1 values that change the dynamic while the note sounds.
  std::vector<int> laterValues() const {
    std::vector<int> values;
    for (const auto &[ms, v] : cc1)
      if (ms > NoteStartSettler::kSettleMs && (!durationMs || ms <= *durationMs)) values.push_back(v);
    return values;
  }

  juce::var toVar(double originMs = 0) const {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("id", juce::int64(id));
    o->setProperty("port", endpoint.first + 1);
    o->setProperty("channel", endpoint.second);
    o->setProperty("pitch", pitch);
    o->setProperty("name", pitchName(pitch));
    o->setProperty("onset_ms", rounded(onsetMs - originMs));
    o->setProperty("duration_ms", durationMs ? juce::var(rounded(*durationMs)) : juce::var());
    o->setProperty("velocity", velocity);
    o->setProperty("dynamics", ccDynamics ? "cc1" : "velocity");
    o->setProperty("pinned", pinned);
    if (const auto level = startLevel()) o->setProperty("level", rounded(*level));
    juce::Array<juce::var> t, lane;
    for (const auto &id : techniques) t.add(juce::String(id));
    for (const auto &[ms, v] : cc1) lane.add(juce::Array<juce::var>{rounded(ms), v});
    o->setProperty("techniques", t);
    o->setProperty("cc1", lane);
    return o.get();
  }
};

struct Tolerances {
  double onsetMs = 5, durationMs = 5, level = 0.2;
};

// Compares received notes with an expected result. Parts are bound to
// endpoints by their chairs' Dorico instruments; notes are matched per part
// in time order by pitch, after removing the one clock offset between
// Fiddle's host time and the score's start.
class Comparison {
public:
  // Received notes further than this from an expected onset (after the
  // offset) are not taken as that note.
  static constexpr double kMatchWindowMs = 1000.0;

  static juce::var run(const juce::var &expected, const std::vector<ReceivedNote> &received,
                       const std::map<Endpoint, juce::String> &instruments) {
    Comparison c(expected, received, instruments);
    return c.result();
  }

  static bool instrumentMatches(const juce::String &chair, const juce::String &part) {
    return chair == part || chair.startsWith(part + ".");
  }

private:
  struct Part { juce::String id, name, instrument; std::optional<Endpoint> endpoint; juce::String boundBy; };
  struct Check { juce::String name; bool ok; juce::var expected, received; juce::String detail; };
  struct Row {
    const juce::var *note = nullptr;
    int index = 0;
    std::optional<size_t> match; // into received_
    std::vector<Check> checks;
    std::optional<double> level;
  };

  Comparison(const juce::var &expected, const std::vector<ReceivedNote> &received,
             const std::map<Endpoint, juce::String> &instruments)
      : expected_(expected), received_(received), instruments_(instruments) {
    const auto &t = expected["tolerances"];
    if (t.isObject()) {
      tolerances_.onsetMs = double(t.getProperty("onset_ms", tolerances_.onsetMs));
      tolerances_.durationMs = double(t.getProperty("duration_ms", tolerances_.durationMs));
      tolerances_.level = double(t.getProperty("level", tolerances_.level));
    }
    if (const auto *parts = expected["parts"].getArray())
      for (const auto &p : *parts)
        parts_.push_back({p["id"].toString(), p["name"].toString(), p["instrument"].toString(), {}, {}});
    if (const auto *notes = expected["notes"].getArray())
      for (int i = 0; i < notes->size(); ++i) rows_.push_back({&notes->getReference(i), i, {}, {}, {}});
    bind();
    match();
    for (auto &row : rows_) checkNote(row);
    checkHairpinOrder();
  }

  void bind() {
    std::map<Endpoint, int> counts;
    for (const auto &r : received_) ++counts[r.endpoint];
    std::set<Endpoint> used;
    for (auto &part : parts_) {
      std::optional<Endpoint> best;
      for (const auto &[endpoint, count] : counts) {
        const auto chair = instruments_.find(endpoint);
        if (used.count(endpoint) || chair == instruments_.end() || !instrumentMatches(chair->second, part.instrument))
          continue;
        if (!best || count > counts[*best]) best = endpoint;
      }
      if (best) { part.endpoint = best; part.boundBy = "chair instrument"; used.insert(*best); }
    }
    std::vector<Part *> unbound;
    for (auto &part : parts_) if (!part.endpoint) unbound.push_back(&part);
    std::vector<Endpoint> spare;
    for (const auto &[endpoint, count] : counts) if (!used.count(endpoint)) spare.push_back(endpoint);
    if (unbound.size() == 1 && spare.size() == 1) {
      unbound[0]->endpoint = spare[0]; unbound[0]->boundBy = "only remaining endpoint";
    }
  }

  std::vector<size_t> receivedFor(const Part &part) const {
    std::vector<size_t> list;
    if (!part.endpoint) return list;
    for (size_t i = 0; i < received_.size(); ++i)
      if (received_[i].endpoint == *part.endpoint) list.push_back(i);
    std::stable_sort(list.begin(), list.end(), [this](size_t a, size_t b) {
      const auto &x = received_[a], &y = received_[b];
      return x.onsetMs != y.onsetMs ? x.onsetMs < y.onsetMs : x.pitch < y.pitch;
    });
    return list;
  }
  std::vector<Row *> rowsFor(const Part &part) {
    std::vector<Row *> list;
    for (auto &row : rows_) if ((*row.note)["part"].toString() == part.id) list.push_back(&row);
    return list;
  }
  static int pitchOf(const Row *row) { return int((*row->note)["pitch"]); }
  static double onsetOf(const Row *row) { return double((*row->note)["onset_ms"]); }

  void match() {
    // Pass 1: the clock offset. In-order pitch matches (longest common
    // subsequence) give one candidate: the median difference of their onsets.
    std::vector<double> differences;
    for (const auto &part : parts_) {
      const auto exp = rowsFor(part);
      const auto rec = receivedFor(part);
      const size_t n = exp.size(), m = rec.size();
      std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
      for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
          lcs[i][j] = pitchOf(exp[i]) == received_[rec[j]].pitch ? lcs[i + 1][j + 1] + 1
                                                                  : std::max(lcs[i + 1][j], lcs[i][j + 1]);
      for (size_t i = 0, j = 0; i < n && j < m;) {
        if (pitchOf(exp[i]) == received_[rec[j]].pitch && lcs[i][j] == lcs[i + 1][j + 1] + 1) {
          differences.push_back(received_[rec[j]].onsetMs - onsetOf(exp[i])); ++i; ++j;
        } else if (lcs[i + 1][j] >= lcs[i][j + 1]) ++i; else ++j;
      }
    }
    // A missing note in a run of one pitch can shift that alignment, so the
    // candidates also include same-pitch pairs among each part's first
    // notes; the one that lines up the most notes wins, then the median of
    // the differences it lines up refines it.
    std::vector<double> candidates;
    if (!differences.empty()) candidates.push_back(median(differences));
    for (const auto &part : parts_) {
      const auto exp = rowsFor(part);
      const auto rec = receivedFor(part);
      for (size_t i = 0; i < std::min<size_t>(exp.size(), 8); ++i)
        for (size_t j = 0; j < std::min<size_t>(rec.size(), 8); ++j)
          if (pitchOf(exp[i]) == received_[rec[j]].pitch)
            candidates.push_back(received_[rec[j]].onsetMs - onsetOf(exp[i]));
    }
    const double window = std::max(10.0, 2 * tolerances_.onsetMs);
    size_t bestCount = 0;
    for (const auto candidate : candidates) {
      std::vector<double> lined;
      for (const auto &part : parts_) {
        const auto rec = receivedFor(part);
        for (const auto *row : rowsFor(part))
          for (const auto j : rec) {
            const double d = received_[j].onsetMs - candidate - onsetOf(row);
            if (received_[j].pitch == pitchOf(row) && std::abs(d) <= window) { lined.push_back(candidate + d); break; }
          }
      }
      if (lined.size() > bestCount) { bestCount = lined.size(); offsetMs_ = median(lined); }
    }
    if (bestCount == 0 && !candidates.empty()) offsetMs_ = candidates.front();
    else if (candidates.empty() && !received_.empty())
      offsetMs_ = std::min_element(received_.begin(), received_.end(), [](const auto &a, const auto &b) {
                    return a.onsetMs < b.onsetMs; })->onsetMs;
    // Pass 2: per part, the in-order matching with the most notes, then the
    // smallest total onset error, among same-pitch pairs within the window.
    std::set<size_t> matched;
    for (const auto &part : parts_) {
      const auto exp = rowsFor(part);
      const auto rec = receivedFor(part);
      const size_t n = exp.size(), m = rec.size();
      struct Score { int count = 0; double error = 0; };
      const auto better = [](const Score &a, const Score &b) {
        return a.count != b.count ? a.count > b.count : a.error < b.error - 1e-9;
      };
      std::vector<std::vector<Score>> best(n + 1, std::vector<Score>(m + 1));
      for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;) {
          Score s = better(best[i + 1][j], best[i][j + 1]) ? best[i + 1][j] : best[i][j + 1];
          if (const auto e = pairError(exp[i], rec[j])) {
            Score take{best[i + 1][j + 1].count + 1, best[i + 1][j + 1].error + *e};
            if (better(take, s)) s = take;
          }
          best[i][j] = s;
        }
      for (size_t i = 0, j = 0; i < n && j < m;) {
        const auto e = pairError(exp[i], rec[j]);
        const auto &here = best[i][j];
        if (e && here.count == best[i + 1][j + 1].count + 1
            && std::abs(here.error - (best[i + 1][j + 1].error + *e)) < 1e-6) {
          exp[i]->match = rec[j]; matched.insert(rec[j]); ++i; ++j;
        } else if (here.count == best[i + 1][j].count && std::abs(here.error - best[i + 1][j].error) < 1e-6) ++i;
        else ++j;
      }
    }
    for (size_t i = 0; i < received_.size(); ++i)
      if (!matched.count(i)) extras_.push_back(i);
  }
  std::optional<double> pairError(const Row *row, size_t r) const {
    const auto &note = received_[r];
    if (note.pitch != pitchOf(row)) return std::nullopt;
    const double delta = std::abs(note.onsetMs - offsetMs_ - onsetOf(row));
    if (delta > kMatchWindowMs) return std::nullopt;
    return delta;
  }

  void checkNote(Row &row) {
    if (!row.match) return;
    const auto &note = *row.note;
    const auto &r = received_[*row.match];
    const auto add = [&row](juce::String name, bool ok, juce::var expected, juce::var received,
                            juce::String detail = {}) {
      row.checks.push_back({std::move(name), ok, std::move(expected), std::move(received), std::move(detail)});
    };
    const double onset = r.onsetMs - offsetMs_;
    const double onsetDelta = onset - double(note["onset_ms"]);
    add("onset", std::abs(onsetDelta) <= tolerances_.onsetMs, note["onset_ms"], rounded(onset),
        signedMs(onsetDelta));
    if (r.durationMs) {
      const double delta = *r.durationMs - double(note["duration_ms"]);
      add("duration", std::abs(delta) <= tolerances_.durationMs, note["duration_ms"], rounded(*r.durationMs),
          signedMs(delta));
    } else {
      add("duration", false, note["duration_ms"], {}, "the note never ended");
    }
    add("pinned_playback", r.pinned, true, r.pinned,
        r.pinned ? juce::String() : "the bridge on this endpoint does not pin Dorico's playback options");

    const auto &dynamic = note["dynamic"];
    row.level = r.startLevel();
    const juce::var level = row.level ? juce::var(rounded(*row.level)) : juce::var();
    const juce::String valueText = r.startValue() ? (r.ccDynamics ? "CC1 " : "velocity ") + juce::String(*r.startValue())
                                                  : juce::String("no CC1 at the note's start");
    if (dynamic.hasProperty("level")) {
      const double want = dynamic["level"];
      add("level", row.level && std::abs(*row.level - want) <= tolerances_.level, want, level, valueText);
    }
    if (const auto *bounds = dynamic["between"].getArray(); bounds && bounds->size() == 2) {
      const double low = (*bounds)[0], high = (*bounds)[1];
      add("level_between", row.level && *row.level >= low - tolerances_.level && *row.level <= high + tolerances_.level,
          dynamic["between"], level, valueText);
    }
    if (const auto &during = dynamic["during"]; during.isObject()) {
      if (!r.ccDynamics) {
        add("level_during", true, during, {}, "not checked: this switch has velocity dynamics");
      } else {
        const bool up = during["direction"].toString() == "up";
        const auto later = r.laterValues();
        const auto start = r.startValue();
        bool ok = start && !later.empty() && (up ? later.back() > *start : later.back() < *start);
        juce::String detail = later.empty() ? juce::String("CC1 did not change during the note")
                                             : "CC1 " + (start ? juce::String(*start) : juce::String("?")) + " to "
                                                   + juce::String(later.back());
        if (ok && !during["to"].isVoid()) {
          const double to = during["to"];
          for (const auto v : later)
            if (up ? bridgeDynamicLevel(v) > to + tolerances_.level : bridgeDynamicLevel(v) < to - tolerances_.level) {
              ok = false; detail << ", beyond the hairpin's end (level " << rounded(bridgeDynamicLevel(v), 0.01) << ")";
              break;
            }
        }
        // Where the hairpin's straight line is when the note ends.
        if (ok && during.hasProperty("end_level")) {
          const double want = during["end_level"], got = bridgeDynamicLevel(later.back());
          if (std::abs(got - want) > tolerances_.level) {
            ok = false;
            detail << ", ends at level " << rounded(got, 0.01) << " instead of " << want;
          }
        }
        add("level_during", ok, during, later.empty() ? juce::var() : juce::var(rounded(bridgeDynamicLevel(later.back()))),
            detail);
      }
    }

    const auto &techniques = note["techniques"];
    const std::set<std::string> have(r.techniques.begin(), r.techniques.end());
    juce::StringArray missing, unwanted;
    if (const auto *list = techniques["includes"].getArray())
      for (const auto &t : *list) if (!have.count(t.toString().toStdString())) missing.add(t.toString());
    if (const auto *list = techniques["excludes"].getArray())
      for (const auto &t : *list) if (have.count(t.toString().toStdString())) unwanted.add(t.toString());
    juce::Array<juce::var> got;
    for (const auto &t : r.techniques) got.add(juce::String(t));
    juce::String detail;
    if (!missing.isEmpty()) detail << "missing " << missing.joinIntoString(", ");
    if (!unwanted.isEmpty()) detail << (detail.isEmpty() ? "" : "; ") << "unexpected " << unwanted.joinIntoString(", ");
    add("techniques", missing.isEmpty() && unwanted.isEmpty(), techniques, got, detail);
  }

  // Levels inside one hairpin move one way.
  void checkHairpinOrder() {
    std::map<std::pair<juce::String, int>, Row *> previous;
    for (auto &row : rows_) {
      const auto &dynamic = (*row.note)["dynamic"];
      if (!dynamic.hasProperty("hairpin") || !row.match || !row.level) continue;
      const auto key = std::make_pair((*row.note)["part"].toString(), int(dynamic["hairpin"]));
      if (const auto prior = previous.find(key); prior != previous.end()) {
        const bool up = dynamic["direction"].toString() == "up";
        const double before = *prior->second->level, now = *row.level;
        const bool ok = up ? now >= before - tolerances_.level : now <= before + tolerances_.level;
        row.checks.push_back({"hairpin_order", ok, dynamic["direction"], rounded(now),
                              "previous note in the hairpin: level " + juce::String(rounded(before, 0.01))});
      }
      previous[key] = &row;
    }
  }

  static double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const size_t k = values.size();
    return k % 2 ? values[k / 2] : (values[k / 2 - 1] + values[k / 2]) / 2;
  }
  static juce::String signedMs(double delta) {
    return (delta >= 0 ? "+" : "") + juce::String(rounded(delta, 0.1)) + " ms";
  }
  juce::String noteLabel(const juce::var &note) const {
    return "#" + note["index"].toString() + " " + note["part"].toString() + " bar " + note["bar"].toString()
           + " beat " + juce::String(double(note["beat"]), 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".")
           + " " + note["source"].toString();
  }

  juce::var result() const {
    juce::Array<juce::var> notes, extras, failures, bindings;
    int passed = 0, failed = 0, missing = 0;
    for (const auto &part : parts_) {
      juce::DynamicObject::Ptr b = new juce::DynamicObject();
      b->setProperty("part", part.id);
      b->setProperty("instrument", part.instrument);
      if (part.endpoint) {
        b->setProperty("port", part.endpoint->first + 1);
        b->setProperty("channel", part.endpoint->second);
        const auto chair = instruments_.find(*part.endpoint);
        b->setProperty("chair_instrument", chair != instruments_.end() ? juce::var(chair->second) : juce::var());
        b->setProperty("bound_by", part.boundBy);
      } else {
        failures.add(part.id + ": no notes arrived from a chair playing " + part.instrument);
      }
      bindings.add(b.get());
    }
    for (const auto &row : rows_) {
      const auto &note = *row.note;
      juce::DynamicObject::Ptr o = new juce::DynamicObject();
      for (const auto *key : {"index", "part", "pitch", "bar", "beat", "source"})
        o->setProperty(key, note[key]);
      juce::Array<juce::var> checks;
      juce::StringArray bad;
      for (const auto &c : row.checks) {
        juce::DynamicObject::Ptr co = new juce::DynamicObject();
        co->setProperty("check", c.name);
        co->setProperty("ok", c.ok);
        co->setProperty("expected", c.expected);
        co->setProperty("received", c.received);
        if (c.detail.isNotEmpty()) co->setProperty("detail", c.detail);
        checks.add(co.get());
        if (!c.ok) bad.add(c.name + (c.detail.isNotEmpty() ? " (" + c.detail + ")" : juce::String()));
      }
      juce::String status = !row.match ? "missing" : bad.isEmpty() ? "pass" : "fail";
      if (!row.match) { ++missing; failures.add(noteLabel(note) + ": missing"); }
      else if (bad.isEmpty()) ++passed;
      else { ++failed; failures.add(noteLabel(note) + ": " + bad.joinIntoString("; ")); }
      o->setProperty("status", status);
      if (row.match) o->setProperty("received", received_[*row.match].toVar(offsetMs_));
      o->setProperty("checks", checks);
      notes.add(o.get());
    }
    for (const auto i : extras_) {
      extras.add(received_[i].toVar(offsetMs_));
      const auto &r = received_[i];
      failures.add("extra note " + pitchName(r.pitch) + " on port " + juce::String(r.endpoint.first + 1) + " channel "
                   + juce::String(r.endpoint.second) + " at " + juce::String(rounded(r.onsetMs - offsetMs_, 0.1)) + " ms");
    }
    juce::DynamicObject::Ptr summary = new juce::DynamicObject();
    summary->setProperty("expected", int(rows_.size()));
    summary->setProperty("received", int(received_.size()));
    summary->setProperty("passed", passed);
    summary->setProperty("failed", failed);
    summary->setProperty("missing", missing);
    summary->setProperty("extra", int(extras_.size()));
    const bool pass = !rows_.empty() && failed == 0 && missing == 0 && extras_.empty();
    juce::DynamicObject::Ptr tol = new juce::DynamicObject();
    tol->setProperty("onset_ms", tolerances_.onsetMs);
    tol->setProperty("duration_ms", tolerances_.durationMs);
    tol->setProperty("level", tolerances_.level);
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("pass", pass);
    o->setProperty("summary", summary.get());
    o->setProperty("offset_ms", rounded(offsetMs_));
    o->setProperty("tolerances", tol.get());
    o->setProperty("bindings", bindings);
    o->setProperty("failures", failures);
    o->setProperty("notes", notes);
    o->setProperty("extras", extras);
    return o.get();
  }

  const juce::var &expected_;
  const std::vector<ReceivedNote> &received_;
  const std::map<Endpoint, juce::String> &instruments_;
  Tolerances tolerances_;
  std::vector<Part> parts_;
  std::vector<Row> rows_;
  std::vector<size_t> extras_;
  double offsetMs_ = 0;
};

// Collects what the decoder delivers between Arm and Done. Called from the
// MIDI thread (under the tracker lock) and the message thread.
//
// A take is one playthrough: transport stop ends it, and the next note or
// transport start after a stop discards it, so Done compares the latest
// playthrough. Notes still sounding at Done have no duration.
class Recorder {
public:
  static constexpr size_t kMaxMidiEvents = 200000;

  struct Take {
    juce::String testId;
    juce::var expected;
    std::map<Endpoint, juce::String> instruments;
    std::vector<ReceivedNote> notes;
    juce::Array<juce::var> midi; // [ms, port, channel, kind, a, b]
    double originMs = 0;         // host time of midi[0]
    double sampleRate = 44100;
    int discardedTakes = 0;
    size_t droppedMidiEvents = 0;
    juce::Time armedAt;
  };

  void setSampleRate(double rate) {
    if (!(rate > 0)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    sampleRate_ = rate;
  }

  void arm(const juce::String &testId, const juce::var &expected, std::map<Endpoint, juce::String> instruments) {
    std::lock_guard<std::mutex> lock(mutex_);
    take_ = Take{};
    take_.testId = testId;
    take_.expected = expected;
    take_.instruments = std::move(instruments);
    take_.armedAt = juce::Time::getCurrentTime();
    sounding_.clear();
    takeClosed_ = false;
    armed_.store(true, std::memory_order_release);
  }
  bool armed() const { return armed_.load(std::memory_order_acquire); }
  // Progress while armed: notes in the current take, and takes discarded.
  std::pair<int, int> progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {int(take_.notes.size() + sounding_.size()), take_.discardedTakes};
  }

  void cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    armed_.store(false, std::memory_order_release);
    take_ = Take{};
    sounding_.clear();
  }

  // Disarms and returns the latest take (nothing if not armed).
  std::optional<Take> finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!armed()) return std::nullopt;
    armed_.store(false, std::memory_order_release);
    for (const auto &[id, note] : sounding_) take_.notes.push_back(note);
    sounding_.clear();
    std::stable_sort(take_.notes.begin(), take_.notes.end(),
                     [](const auto &a, const auto &b) { return a.onsetMs < b.onsetMs; });
    take_.sampleRate = sampleRate_;
    return std::move(take_);
  }

  void noteStarted(const Note &note) {
    if (!armed()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!armed()) return;
    beginNewTakeIfClosed();
    sounding_[note.id()] = ReceivedNote::from(note, sampleRate_, false);
  }
  void noteEnded(const Note &note) {
    if (!armed()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!armed() || !sounding_.erase(note.id())) return;
    take_.notes.push_back(ReceivedNote::from(note, sampleRate_, true));
  }
  // Every event the tracker saw, bridge commands included.
  void midiEvent(const MidiEvent &event, uint64_t absoluteSamples, bool bridgeCommand) {
    if (!armed()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!armed()) return;
    juce::String kind;
    int a = 0, b = 0;
    if (event.has_note_on()) { kind = event.note_on().velocity() ? "on" : "off"; a = event.note_on().note_number(); b = event.note_on().velocity(); }
    else if (event.has_note_off()) { kind = "off"; a = event.note_off().note_number(); b = event.note_off().velocity(); }
    else if (event.has_cc()) { kind = "cc"; a = event.cc().controller_number(); b = event.cc().controller_value(); }
    else if (event.has_pitch_bend()) { kind = "pb"; a = event.pitch_bend().value(); }
    else if (event.has_program_change()) { kind = "pc"; a = event.program_change().program_number(); }
    else if (event.has_transport()) {
      const bool start = event.transport().type() == MidiEvent_TransportEvent_Type_START;
      kind = start ? "start" : "stop";
      if (start) beginNewTakeIfClosed();
      else takeClosed_ = true;
    } else if (event.has_tempo()) { kind = "tempo"; a = int(std::lround(event.tempo().bpm() * 1000)); }
    else return;
    if (bridgeCommand) kind = "bridge " + kind;
    if (take_.midi.size() >= int(kMaxMidiEvents)) { ++take_.droppedMidiEvents; return; }
    // Transport start carries no position: stamp it with the last one.
    const double ms = absoluteSamples ? double(absoluteSamples) * 1000.0 / sampleRate_ : lastMs_;
    if (take_.midi.isEmpty()) take_.originMs = ms;
    lastMs_ = ms;
    take_.midi.add(juce::Array<juce::var>{rounded(ms - take_.originMs), int(event.port()) + 1,
                                          int(event.channel()), kind, a, b});
  }

private:
  void beginNewTakeIfClosed() {
    if (!takeClosed_) return;
    takeClosed_ = false;
    if (take_.notes.empty() && sounding_.empty()) return;
    take_.notes.clear();
    take_.midi.clear();
    take_.droppedMidiEvents = 0;
    sounding_.clear();
    ++take_.discardedTakes;
  }

  mutable std::mutex mutex_;
  std::atomic<bool> armed_{false};
  Take take_;
  std::map<uint64_t, ReceivedNote> sounding_;
  bool takeClosed_ = false;
  double sampleRate_ = 44100, lastMs_ = 0;
};

// The run record: everything needed to understand a run later, by its ID.
inline juce::var runRecord(const juce::String &runId, const Recorder::Take &take, const juce::var &context = {}) {
  const auto comparison = Comparison::run(take.expected, take.notes, take.instruments);
  juce::Array<juce::var> received, chairs;
  for (const auto &note : take.notes) received.add(note.toVar(take.originMs));
  for (const auto &[endpoint, instrument] : take.instruments) {
    juce::DynamicObject::Ptr c = new juce::DynamicObject();
    c->setProperty("port", endpoint.first + 1);
    c->setProperty("channel", endpoint.second);
    c->setProperty("instrument", instrument);
    chairs.add(c.get());
  }
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("schema_version", 1);
  o->setProperty("run_id", runId);
  o->setProperty("test_id", take.testId);
  o->setProperty("title", take.expected["title"]);
  o->setProperty("armed_at", take.armedAt.toISO8601(true));
  o->setProperty("finished_at", juce::Time::getCurrentTime().toISO8601(true));
  o->setProperty("pass", comparison["pass"]);
  o->setProperty("summary", comparison["summary"]);
  o->setProperty("failures", comparison["failures"]);
  o->setProperty("sample_rate", take.sampleRate);
  o->setProperty("discarded_takes", take.discardedTakes);
  o->setProperty("dropped_midi_events", juce::int64(take.droppedMidiEvents));
  o->setProperty("context", context);
  o->setProperty("chairs", chairs);
  o->setProperty("comparison", comparison);
  // Times in received and midi are host milliseconds from the first event,
  // which is origin_ms on the host clock; comparison times subtract offset_ms.
  o->setProperty("origin_ms", rounded(take.originMs));
  o->setProperty("received", received);
  o->setProperty("midi_columns", juce::Array<juce::var>{"ms", "port", "channel", "kind", "a", "b"});
  o->setProperty("midi", take.midi);
  o->setProperty("expected", take.expected);
  return o.get();
}

inline juce::String runId(const juce::String &testId, juce::Time at) {
  return testId + "-" + at.formatted("%Y%m%d-%H%M%S");
}

} // namespace fiddle::scoretest
