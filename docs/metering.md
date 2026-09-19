# Stereo metering and Master loudness

Every instrument strip, group/FX bus, and Master has independent L/R sample-peak
bars, slowly decaying peak-hold marks, and latched clip lights. Click either L/R
light to clear that strip's two lights. Master Audio also offers **Clear all clip
indicators**. A sample at or above 0 dBFS latches the corresponding light; floating
point audio is not limited or otherwise changed by these meters.

Strip and bus meters measure their audible output after balance, fader, and
post-fader processing. They do not detect overload inside a plug-in or in a
separate pre-fader send. Master measures the final Fiddle stereo output, before
transfer to Dorico; Dorico's subsequent processing is not measured. Because
Fiddle renders ahead, the displays can lead what you hear by the render-ahead
delay.

The Master strip shows **Integrated** and **Short-term** LUFS beneath its fader
readout, in the space used by Mute/Solo on other strips. Click either reading
(or the **Audio FX** button) to open **Loudness & true peak** in Master Audio.
Missing, warming-up, or incomplete measurements show a dash. The small reset
arrow beside the readings resets the same loudness/true-peak measurement as the
panel's Reset button, without opening the panel or changing the audio. True-peak
details remain in the panel, keeping the strip compact and faders aligned.
Measurement is post-Master-fader; fader changes do not automatically reset the
accumulated history. Set the level, reset, and replay to measure the revised mix.

- **Integrated LUFS:** gated programme loudness since Reset measurement; needs
  at least 400 ms of rendered audio. Silence-only measurements remain blank.
- **Short-term LUFS:** the most recent three seconds of rendered audio; blank
  until three seconds are available.
- **True peak L/R (dBTP):** highest reconstructed peak since reset, including
  peaks between samples. These differ from the strip's sample-clip lights.

Reset just before playing a complete piece and allow its tails to finish. The
measurement includes audition notes and any other rendered Master audio, not
only Dorico playback or a print. It pauses when Fiddle's renderer sleeps; idle
wall-clock time is not added as silence. Audio-device reinitialization starts a
new measurement. Resets and clip flags are runtime-only, not project edits,
saved settings, or Undo actions.

Analysis uses [libebur128 1.2.6](https://github.com/jiixyj/libebur128/tree/v1.2.6)
on a low-priority background thread. The render callback only makes a bounded,
preallocated stereo copy using JUCE vector operations. If its queue overflows
or analysis encounters invalid samples, the UI marks the measurement incomplete
and hides its figures until reset; analysis never waits on the audio thread.
The library's license is included in the application Resources directory.

## Smoke test

1. Restart Fiddle. Play a stereo patch, then move its balance left and right;
   check that its bars, routed bus bars, and Master bars respond independently.
2. In a safe test project with monitoring turned down, briefly raise a level
   above 0 dBFS. Restore a safe level. The red L/R lights should stay lit until
   clicked; the Master panel's clear-all button should clear every strip.
3. Open Master Audio, Reset measurement, and play a piece. Integrated LUFS
   should appear after the first audio; short-term after three seconds. True
   peak maxima should persist after playback. Reset should clear the readings.
4. Check Compact, Comfortable, and Large strip sizes; fader alignment and all
   existing controls should remain unchanged. Confirm a normal WAV print and
   Dorico playback still sound as before.

Automated coverage checks unequal channels, decay and latching, reset/retrigger,
known sine loudness, worker/reference parity at 44.1/48/96 kHz, intersample peaks,
pending-buffer reset, overflow and invalid-sample reporting, and browser layout
and reset commands. These are regression checks, not a certification of the
complete application against a broadcast conformance suite.
