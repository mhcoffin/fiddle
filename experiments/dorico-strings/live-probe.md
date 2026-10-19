# Live input verification: Fiddle MIDI Probe

> **Historical capture/setup procedure, not current installation instructions.**
> The original captures were completed; see [live results](live-results.md).
> The probe remains a reusable diagnostic, but new tests must explicitly choose
> their setup and endpoint. The active plan is
> [Orchestral bridge maps](../../docs/orchestral-bridge-maps.md).

The separate **Fiddle MIDI Probe** VST3 is capture-only. It has distinct component
and controller IDs, reports zero audio latency, produces silence, and never
creates the TCP relay or reads shared audio. It cannot drive Fiddle instruments
or replace the normal Fiddle plug-in in an existing score. It uses the exact
same new host-input adapter as the rebuilt normal plug-in, but does **not**
exercise the TCP/server/audio-rendering stages.

## Before the user's test

Build target: `FiddleMidiProbe`. Bundle:
`build/release/FiddleMidiProbe_artefacts/VST3/Fiddle MIDI Probe.vst3`.

The build does **not** install anything. Close Dorico before installing only
this additional bundle in the user's VST3 folder. Leave the installed normal
`Fiddle.vst3` untouched for now. If macOS signing is required, sign the diagnostic
bundle before installation. Its module metadata and factory IDs must agree.

## First live pass

1. Reopen the disposable 16-note Dorico test project, preferably a saved copy.
2. Load **Fiddle MIDI Probe** as its VST instrument. No custom editor is needed;
   a generic parameter panel or no editor is expected.
3. Route the test violin to its **Port 1, Channel 1**, with expression map
   **Fiddle EXP Strings F - high vibrato r6**. Do not change the notation or
   route this map into ordinary Fiddle.
4. Play once from the beginning, then stop. Silence is intentional.
5. Close that project and quit Dorico to ensure the probe is deactivated and
   the trace is closed. Tell the assistant the pass is finished.

Each activation writes a new private directory beneath the OS temporary folder:
`fiddle-midi-probe-<unique-id>/capture.jsonl`. On macOS this may be under the
per-user `/var/folders/.../T` directory, not `/tmp`. Locate it with Python's
`tempfile.gettempdir()` or shell `$TMPDIR`; do not delete unrelated temp files.
Dorico scanning/unused instances can create empty traces. Select a closed trace
that actually contains the test notes. No capture is uploaded or sent over TCP.

Analysis from the repository root:

```sh
python3 experiments/dorico-strings/inspect_live.py /absolute/path/capture.jsonl
```

The report distinguishes exact PT-set agreement, missing state, late updates,
and timing cautions. A nonzero exit can mean a cross-list ordering caution even
when PT sets match. **Do not report full timing acceptance from PT sets alone.**

## Subsequent mid-passage pass

After analyzing the first capture, start a fresh probe activation and play from
note 12 through note 16. This checks reconstruction without borrowing a prior
playthrough's state. Close Dorico afterward and analyze with `--from-note 12`.
If multiple start/stop passes were captured, `--pass 2` selects the second one;
the inspector deliberately starts decoding afresh after the previous STOP.
Do not invent initialization when Dorico omits it. If selecting a passage
auditions a musical note, it remains in the raw trace. The report explicitly
counts note-ons outside the playing transport interval and excludes them from
that pass's expected musical notes, retaining preparatory protocol commands.
Missing/incorrect host transport flags require investigation.

## Input-path corrections in this build

- Keep every parameter point, not just the last CC/program value per block.
- Merge parameter and note events by sample offset. Preserve parameter queue/
  point order and event-list order within equal offsets. At cross-list ties,
  parameters precede event-list messages; VST3 provides no total order between
  those lists, so this is an explicit policy, not an observed host ordering.
- Round normalized velocities/pressure to the nearest MIDI integer; reject
  non-finite/out-of-range inputs. Keep raw normalized values in the probe trace.
- Use `projectTimeSamples` whenever a process context exists, independently of
  the validity flag for musical/quarter-note time. Record signed host positions
  in trace block metadata; the legacy unsigned wire saturates negative preroll.
- Send transport START before merged musical data and STOP after the block.
- Bound adapter work/storage to 16,384 attempted inputs per block and report
  dropped/invalid inputs in normal diagnostics and the probe. No allocation,
  file I/O, or capture mutex occurs in `process()`.

The probe's queue holds 32,768 records, and capture is capped at 250,000 records
per activation. File I/O and flushing run on a worker. Dropped records, unsupported
input types, missing footer, malformed records, and invalid values prevent an
acceptance result; they are not silently ignored. Keep tests short.

The probe itself does not validate TCP delivery, server decoding, or per-strip
instrument output. Subsequent opt-in decoder work and server replay results are
documented in [live-results.md](live-results.md). The map remains incompatible
with a normal (non-opted-in) server launch.

## Local checks

```sh
cmake --build build/release --target HostMidiBlockTest FiddleNative MidiProbeSmokeTest --parallel 4
ctest --test-dir build/release -R '^(host_midi_block|midi_probe_smoke)$' --output-on-failure
python3 -m unittest discover -s experiments/dorico-strings -p 'test_*.py'
```

The smoke test loads the actual diagnostic bundle, checks its factory identity,
activates it, verifies silence/zero latency, then checks closed capture output.
It never installs a plug-in. Unit tests cover conversion, multiple points, merge
order, invalid data, capacity overflow, capture lifecycle and trace validation.
