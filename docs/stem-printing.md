# Real-time stem printing

Open **View → Windows → Print Audio** in the application menu, or **Print Audio**
under the **Windows** heading in the mixer toolbar's View menu.
Printing has its own non-modal window, separate from Master Audio effects.
Closing the window hides it without cancelling or stopping a take; reopen it
from View to check progress or finish. Hidden windows stop polling print status.

Choose **Selected bus stems**, check the
group/FX buses to export, and optionally include **Master mix**. Choose a parent
folder and arm. Each take creates a new timestamped, uniquely named subfolder;
existing stem files are never overwritten. Numbered, sanitized names distinguish
even buses with identical names. Up to 64 stereo, 32-bit floating-point WAVs
can be printed at the active audio sample rate.

Select the desired passage in Dorico before arming. The next transport Start
opens one capture gate for every file. **Tail after Stop** records effects for
an additional duration after Dorico stops: 5 seconds by default, adjustable
from 0 to 60 seconds before arming. Set 0 for an exact transport-stop cutoff,
or increase it for a long reverb. The renderer stays active during the tail;
all files close at the same sample, including silent stems. This is a fixed
duration, not automatic silence detection. Wait for completion before playing
or auditioning another passage, because that audio would also be recorded.
Manual **Stop & finish** closes all files immediately, even during a tail,
without stopping playback. Cancelling
before playback leaves valid empty WAVs in the take folder.

## What is in each file?

- Bus stems contain the bus's direct inputs and sends, inserts (pre and post),
  balance and fader. They respect the live mute/solo mix, including solo-safe FX
  returns. Printing never changes routing, solos tracks, or renders instruments
  an extra time.
- Master contains the complete final output, including Master inserts and gain.
- Bus stems exclude Master processing. They are delayed by the Master rack's
  latency so their audio aligns with the Master file, not shifted earlier.
- Shared reverb/other FX buses are separate wet stems, not folded into each
  contributing instrument/group stem.
- Instruments routed directly to Master are present only in the Master file.
  To obtain an instrument/family stem, first route it through a group bus.
- Summing bus files does not necessarily recreate Master: direct-to-Master
  sources may be absent, and Master gain/effects (especially nonlinear effects)
  are not baked into stems. Do not sum Master together with its stems.

## Safety and implementation

Selection and names are captured when arming. A single multichannel FIFO accepts
all selected outputs atomically, feeding stereo writers on one low-priority
thread. Buffers and Master-latency delay lines are allocated at arming, not on
the render thread. FIFO storage is capped at 64 MiB, up to eight seconds at
smaller channel counts; latency compensation is separately capped at 64 MiB.
No periodic WAV header rewrites occur during recording. Stop drains queued
audio and finalizes every file.

Queue overflow, disk-write/finalization failure, a render timeline discontinuity,
removal of a selected bus, or a change to Master latency makes the entire take
incomplete. Fiddle reports the error and ends recording instead of silently
allowing independently shortened stems. Partial files remain for inspection;
rearm and replay for a complete take. A disk failure can leave unequal partial
files, so the entire take must be treated as invalid. Other output audio remains
unaffected by disk admission failures.

Continuity is checked using integer stream sample positions, not callback
timestamps (which jitter with host scheduling). Missing or repeated samples
during capture still invalidate the take; gaps before the capture gate opens
do not. Presentation timestamps are used only to trim the transport boundaries.

Avoid routing, plug-in, device, and project changes during a take. Ordinary
level/mute/solo changes are printed as heard. Renaming a bus does not rename an
already-open file. A lost Dorico connection ends the take with a visible error.

## Smoke test

1. Route two instruments to two group buses; send both to a shared reverb bus.
2. Stop playback. Choose those three buses plus Master in the print section,
   choose a parent folder, and arm. Play a short passage and stop Dorico while
   the reverb is still ringing. Confirm **Printing tail** stays active for the
   chosen duration and playback and controls stay responsive.
3. Verify four stereo float WAVs have identical sample counts. Import them at
   the same start time. Group files should be separate, reverb wet-only, Master
   the complete mix. Listen to Master separately from stems.
4. Try bus-only printing, mute/solo behavior, and another take with the same
   names; the previous folder must remain untouched.
5. With a latency-reporting Master effect, verify stem transients still align
   with Master. Master processing must not appear in the bus stems.
6. Test a longer tail, zero tail, and manual **Stop & finish** during a tail.

Automated tests cover trimmed common/tail boundaries, float sample data, duplicate
names, cancellation, Master omission, latency compensation, missing sources,
host-clock jitter, pre-roll gaps, missing/repeated sample positions, FIFO rejection, stop/render lifetime safety, real
mixer routing/mute/solo/sends under 1/2/4 workers, and browser output selection.
