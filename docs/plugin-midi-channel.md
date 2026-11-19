# A layer's plug-in MIDI channel

Status: implemented 2026-11-18.

## Why

Fiddle passes on Dorico's MIDI channel unchanged, so a layer on Dorico port 1,
channel 15 sends its plug-in channel 15. Some plug-ins listen only on channel
1 and have no channel setting. Vienna Performer, for example, captured the
notes it was sent but played nothing. Each layer can now name the channel its
plug-in receives.

## Behaviour

**Layer Details → Instrument → MIDI channel** offers:

- **As Dorico sends (channel N)**, the default. Fiddle passes the channel
  through, as before.
- **Channel 1–16.** Every channel message for the plug-in is moved to that
  channel: notes, controllers, keyswitches, pitch bend and program changes.

Details:

- **Where it applies:** `MixerStrip::addDelayedMessage`, the one place a
  layer's MIDI is scheduled. Expression maps, Lua processors and bridge
  commands still see Dorico's channel, and so do Fiddle's captures. Only the
  plug-in's input changes.
- **Changing it** sends the layer an all-notes-off (`allNotesOff`) first, so no
  note is left sounding on the old channel. A keyswitch already held goes out
  again at the next articulation change or transport start.
- **Undo:** the change is undoable (`SetPluginMidiChannelAction`, IPC
  `setStripPluginMidiChannel [stripId, channel]`).
- **Scope:** the setting is per layer. A library refresh or patch update leaves
  it alone, and it isn't part of the library patch.

## Storage

| Store | Field |
| --- | --- |
| Live layers | `layers.plugin_midi_channel` (`LayerRow::pluginMidiChannel`) |
| Versions | `strip_blobs.plugin_midi_channel` (`StripBlob::pluginMidiChannel`) |
| State blob (Dorico project) | `pluginMidiChannel`, written only when not 0; restored by `restore_state` |
| UI | `pluginMidiChannel` in each strip's mixer state |

The version hash includes the channel only when it is not 0, so existing
versions keep their hashes. A layer with no live strip is saved from its
routing row (`StateManager::captureRoutingState`'s fallback snapshot), which
carries the channel too.

## Tests

- **`MixerIntegrationTest`:**
  - "a layer's MIDI channel remaps what its plug-in receives" checks the
    remapping and its undo.
  - The database reopen test checks that the channel survives a saved
    version and the state blob.
  - "a fallback snapshot keeps a layer's MIDI channel" covers a layer with no
    live strip.
- **`LibraryRoutingRepositoryTest`** checks that a layer keeps its channel
  through `upsertLayer` and `updateLayerSetups`.
- **`MixerJsHandlersTest`** covers the IPC handler.
