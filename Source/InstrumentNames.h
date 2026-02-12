#pragma once

#include <juce_core/juce_core.h>
#include <string>

namespace fiddle {

/**
 * Get the General MIDI Level 1 instrument name for a given program number.
 * Bank MSB and LSB are included for future extensibility (e.g., GS, XG
 * variations).
 */
inline juce::String getInstrumentName(int bankMSB, int bankLSB, int program) {
  // For now, we only handle General MIDI (Bank 0)
  // Future: Add support for Roland GS, Yamaha XG, etc.

  if (bankMSB != 0 || bankLSB != 0) {
    // Non-GM bank - return generic name
    return "Program " + juce::String(program);
  }

  // General MIDI Level 1 instrument names (Program 0-127)
  static const char *gmInstruments[128] = {
      // Piano (0-7)
      "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano",
      "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord",
      "Clavi",

      // Chromatic Percussion (8-15)
      "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba",
      "Xylophone", "Tubular Bells", "Dulcimer",

      // Organ (16-23)
      "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
      "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",

      // Guitar (24-31)
      "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)",
      "Electric Guitar (jazz)", "Electric Guitar (clean)",
      "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar",
      "Guitar harmonics",

      // Bass (32-39)
      "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)",
      "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1",
      "Synth Bass 2",

      // Strings (40-47)
      "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings",
      "Pizzicato Strings", "Orchestral Harp", "Timpani",

      // Ensemble (48-55)
      "String Ensemble 1", "String Ensemble 2", "Synth Strings 1",
      "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice",
      "Orchestra Hit",

      // Brass (56-63)
      "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn",
      "Brass Section", "Synth Brass 1", "Synth Brass 2",

      // Reed (64-71)
      "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe",
      "English Horn", "Bassoon", "Clarinet",

      // Pipe (72-79)
      "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi",
      "Whistle", "Ocarina",

      // Synth Lead (80-87)
      "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)",
      "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)",
      "Lead 8 (bass + lead)",

      // Synth Pad (88-95)
      "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
      "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",

      // Synth Effects (96-103)
      "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
      "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",

      // Ethnic (104-111)
      "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bag pipe", "Fiddle",
      "Shanai",

      // Percussive (112-119)
      "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum",
      "Melodic Tom", "Synth Drum", "Reverse Cymbal",

      // Sound Effects (120-127)
      "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
      "Telephone Ring", "Helicopter", "Applause", "Gunshot"};

  if (program >= 0 && program < 128) {
    return juce::String(gmInstruments[program]);
  }

  return "Unknown";
}

} // namespace fiddle
