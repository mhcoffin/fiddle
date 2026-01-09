#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace fiddle {

class MidiPlayer {
public:
  MidiPlayer() {}

  void loadFile(const juce::File &file) {
    juce::FileInputStream stream(file);
    if (stream.openedOk()) {
      juce::MidiFile midiFile;
      if (midiFile.readFrom(stream)) {
        midiFile.convertTimestampTicksToSeconds();
        sequence.clear();
        for (int i = 0; i < midiFile.getNumTracks(); ++i) {
          sequence.addSequence(*midiFile.getTrack(i), 0, 0,
                               midiFile.getLastTimestamp());
        }
        sequence.updateMatchedPairs();
        durationSeconds = sequence.getEndTime();
        playheadSeconds = 0;
        std::cerr << "[MidiPlayer] Loaded " << sequence.getNumEvents()
                  << " events. Duration: " << durationSeconds << "s"
                  << std::endl;
      } else {
        std::cerr << "[MidiPlayer] Error: Could not read MIDI file content"
                  << std::endl;
      }
    } else {
      std::cerr << "[MidiPlayer] Error: Could not open file "
                << file.getFullPathName() << std::endl;
    }
  }

  void start() { playing = true; }
  void pause() { playing = false; }
  void rewind() { playheadSeconds = 0; }
  void setPosition(double seconds) { playheadSeconds = seconds; }

  void getNextBlock(juce::MidiBuffer &buffer, int numSamples,
                    double sampleRate) {
    if (!playing)
      return;

    double startTime = playheadSeconds;
    double endTime = startTime + (double)numSamples / sampleRate;

    int startSample = 0;

    // Iterate through sequence events between startTime and endTime
    for (int i = 0; i < sequence.getNumEvents(); ++i) {
      auto *event = sequence.getEventPointer(i);
      double eventTime = event->message.getTimeStamp();

      if (eventTime >= startTime && eventTime < endTime) {
        int offset = (int)((eventTime - startTime) * sampleRate);
        buffer.addEvent(event->message,
                        juce::jlimit(0, numSamples - 1, offset));
      }
    }

    playheadSeconds = endTime;
    if (playheadSeconds >= durationSeconds) {
      playing = false;
      playheadSeconds = durationSeconds;
    }
  }

  bool isPlaying() const { return playing; }
  double getDuration() const { return durationSeconds; }
  double getPosition() const { return playheadSeconds; }

private:
  juce::MidiMessageSequence sequence;
  bool playing = false;
  double playheadSeconds = 0;
  double durationSeconds = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlayer)
};

} // namespace fiddle
