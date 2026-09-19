# Playback UI responsiveness

## September 2026 diagnosis

During a reported Master Audio freeze, Fiddle's native message thread remained
mostly idle (394/433 samples waiting in the run loop). The loudness worker was
waiting in 425/433 samples. Two WebKit content processes were busy executing
incoming JavaScript; one also spent much of its time in microtask processing.
This was not a sampled native lock stall in Reset measurement.

Previously every MIDI event, note update and log entry was broadcast into every
webview, including the mixer and Library Manager, even though their debug panes
were absent. Every event copied a deeply reactive MIDI history of up to 1,000
entries. The sender had no browser backpressure for this stream.

## Revised contract

- MIDI/note/log/beat/tempo display messages go only to the visible, ready Debug
  window, never the main mixer, Library Manager or History window. The receiving
  IPC layer also ignores these messages outside debug mode.
- Producers enqueue into a bounded 512-message diagnostic queue, without posting
  a native message-thread callback for every event. The message-thread timer
  sends at most 128 recent messages every 100 ms, with only one WebKit batch in
  flight. When overloaded, old display updates are discarded instead of replayed.
- Hiding/minimizing Debug stops collection. Reopening starts a fresh live view
  with the most recently observed tempo; it does not replay hidden activity.
- Missing transitions reset live note history to avoid stuck display notes. A
  banner reports skipped updates. This is explicitly a best-effort live view;
  MIDI routing, the backend MIDI Capture facility, and audio rendering do not
  use this queue and are unchanged.
- Debug arrays use shallow immutable state instead of deep reactive proxies.
  MIDI history remains capped at 1,000 events, logs at 200, and note/beat/tempo
  histories at 4,096 entries. Svelte applies a batch's state changes together.
- Meter updates retain their own coalesced, single-in-flight path. Master fader
  and Reset commands are not placed in the diagnostic queue.

## Manual verification

Restart the rebuilt Release Fiddle server; no Dorico plug-in reinstall is needed.
Play the same busy passage with Master Audio open. Repeatedly click Reset
measurement and move the Master output fader to a safe level. Confirm that meters,
LUFS, and controls remain responsive. Repeat with Library Manager open, then with
Debug's Event Log/Timeline visible. Hide/reopen Debug and verify that it resumes a
fresh live view. Under an excessive event load its skipped-display warning is
expected; it must not stall the mixer or affect recorded MIDI/audio.

Automated tests cover queue bounds, overflow accounting, concurrent producers,
single-in-flight delivery, hide/show while a batch is outstanding, retry after
evaluation failure, diagnostic-only dispatch, non-debug-window isolation, and
actual browser control/meter responsiveness under a synthetic event flood.
