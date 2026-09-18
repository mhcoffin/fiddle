# Stereo balance

Instrument channels and group/FX buses have a needle-only Balance knob (no level
ring) and editable readout
immediately above their faders. Center is `C`; `L25` and `R50` mean 25% left and
50% right. Signed numbers (`-25`, `50`) and suffixes (`25L`) also work. Drag the
knob vertically, use arrow keys (1%), or Home/End (full left/right). Shift slows
pointer movement. Click the Balance label to reset to center. A gesture is one
Undo step; Undo/Redo and saved project versions retain the setting.

This is **stereo balance**, not a mono or dual panner: it does not move the right
channel's contents into the left or vice versa. Center preserves the original
stereo signal at unity gain. Turning left progressively attenuates only the
right channel; turning right attenuates only the left. At the extremes the
opposite channel is silent; halfway it is multiplied by 0.5 (about −6 dB).
There is no center attenuation or compensating boost. Existing projects open
centered and sound unchanged. Width, dual-pan, chair-wide linked balance and
Master balance are outside this first implementation.

Signal order: pre-fader inserts → pre-fader send tap → gain and balance →
post-fader inserts → direct output and post-fader sends. Pre-fader sends retain
the original stereo image. Post-fader effects may generate stereo output even
with balance fully to one side. Bus balance applies to its summed direct and
send inputs at the bus fader stage, before that bus's post-fader inserts.

Balance changes ramp over 10 ms across audio blocks. The render path allocates
nothing and adds no latency; steady gains use JUCE's vectorized operations.
Edits use atomic control state without rebuilding routing. Balance-only UI
updates and throttled host-state snapshots reuse cached plug-in state rather
than querying vendor program/state APIs. Other coalesced edits that require a
live snapshot retain their existing behavior.

## Smoke test

1. Restart Fiddle, play one stereo instrument and turn Balance left/right.
   Check center sounds unchanged and click the label to restore it.
2. Type `L25`, then try Undo/Redo. Save and reopen the project; check the value.
3. Use an FX bus with a 100% wet effect. Post-fader sends should follow channel
   balance; a pre-fader send should retain its stereo image. A stereo reverb
   can spread its output again, so don't expect a fully one-sided reverb tail.
4. Balance a group/FX bus and confirm it affects the whole bus, without moving
   the source-channel controls. Check Undo/Redo and save/reopen.

Automated tests cover per-channel samples, unity center/no crossfeed, smoothing
across variable block sizes, pre/post routing, serial/parallel rendering, input
validation, history, database/version/host-state persistence, and all three
strip sizes' control fit and fader alignment.
