#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace fiddle {

/**
 * A single captured MIDI event with a relative timestamp.
 */
struct CapturedMidiEvent {
  enum Type { NoteOn = 0, NoteOff = 1, CC = 2, ProgramChange = 3 };

  double relativeTimeMs = 0.0; // offset from capture start
  int channel = 1;             // 1-based
  Type type = NoteOn;
  int param1 = 0; // note#, CC#, or program#
  int param2 = 0; // velocity or CC value
  juce::String label; // optional annotation (e.g. switch name)

  juce::var toVar() const {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("t", relativeTimeMs);
    obj->setProperty("ch", channel);
    obj->setProperty("type", static_cast<int>(type));
    obj->setProperty("p1", param1);
    obj->setProperty("p2", param2);
    if (label.isNotEmpty())
      obj->setProperty("lbl", label);
    return juce::var(obj);
  }

  static CapturedMidiEvent fromVar(const juce::var &v) {
    CapturedMidiEvent e;
    if (auto *obj = v.getDynamicObject()) {
      e.relativeTimeMs = obj->getProperty("t");
      e.channel = obj->getProperty("ch");
      e.type = static_cast<Type>(static_cast<int>(obj->getProperty("type")));
      e.param1 = obj->getProperty("p1");
      e.param2 = obj->getProperty("p2");
      if (obj->hasProperty("lbl"))
        e.label = obj->getProperty("lbl").toString();
    }
    return e;
  }
};

/**
 * Timestamped MIDI event log for capturing incoming or emitted MIDI
 * on a per-strip basis.  Thread-safe: all methods guard with a mutex.
 */
class MidiCaptureLog {
public:
  void startCapture() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
    startTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    capturing_ = true;
  }

  void stopCapture() {
    std::lock_guard<std::mutex> lock(mutex_);
    capturing_ = false;
  }

  bool isCapturing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capturing_;
  }

  void record(double absoluteTimeMs, int channel, CapturedMidiEvent::Type type,
              int p1, int p2, const juce::String &label = {}) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!capturing_)
      return;
    CapturedMidiEvent e;
    e.relativeTimeMs = absoluteTimeMs - startTimeMs_;
    e.channel = channel;
    e.type = type;
    e.param1 = p1;
    e.param2 = p2;
    e.label = label;
    events_.push_back(e);
  }

  size_t eventCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
    capturing_ = false;
  }

  /** Serialize the entire log to a juce::var (array of event objects). */
  juce::var toVar() const {
    std::lock_guard<std::mutex> lock(mutex_);
    juce::Array<juce::var> arr;
    for (const auto &e : events_)
      arr.add(e.toVar());
    return juce::var(arr);
  }

  /** Deserialize from a juce::var (array of event objects).
   *  Populates this log, replacing any existing events. */
  void loadFromVar(const juce::var &v) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
    capturing_ = false;
    if (auto *arr = v.getArray()) {
      for (const auto &item : *arr)
        events_.push_back(CapturedMidiEvent::fromVar(item));
    }
  }

private:
  mutable std::mutex mutex_;
  std::vector<CapturedMidiEvent> events_;
  double startTimeMs_ = 0.0;
  bool capturing_ = false;
};

} // namespace fiddle
