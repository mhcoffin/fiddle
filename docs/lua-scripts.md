# Lua scripts on layers

A layer can run Lua scripts ("Fiddles", `LuaPlugin.h`) before its expression
map. Scripts are files: Fiddle's own ship in the app bundle
(`scripts/plugins`), and the user's go in
`~/Library/Application Support/Fiddle/plugins`. Layer details lists the scripts
Fiddle found under **Add Lua processor…**.

## Writing a script

A script is a Lua file that returns a table: its name, its parameters, and
up to three functions, all optional.

```lua
local plugin = {
  name = "Humanize", version = "2.0", author = "Fiddle",
  description = "Adds small random changes to each note's velocity, timing and length.",
  params = { ... },                      -- see Parameters
}

function plugin.on_note(note, ctx) ... end  -- each note, as it starts
function plugin.on_cc(cc, ctx) ... end      -- each live controller
function plugin.on_reset(ctx) ... end       -- after loading, and at every playback start

return plugin
```

A function changes what it's given; nothing needs returning. Fiddle checks
everything a function leaves before using any of it: a field the note doesn't
have (`note.velcity`), a field that can't be changed, or a value of the wrong
kind is an error that names it, and the note goes on as it came. Errors go to
the log.

### The note

`on_note` runs once for each note, as it starts (`LuaPlugin::onNote`).
Fiddle hears Dorico live, so a note's length isn't known yet: scripts say how
a note should end instead, and Fiddle applies that at its note-off
(`LuaAnnotator`). The note-off always goes to the pitch and channel the
note-on used, and a dropped note's note-off is dropped too.

| Field | |
|---|---|
| `pitch` | 0–127 |
| `velocity` | 1–127; Fiddle clamps what the script leaves |
| `channel` | 1–16 |
| `start_offset_ms` | moves the note, and its end with it: negative is earlier. At most 500 ms either way, within the playback delay |
| `length_delta_ms` | makes the note longer or shorter. Fiddle never shortens a note below 10 ms |
| `max_length_ms` | the longest the note lasts (at least 10). Fiddle ends a longer note then, and Dorico's later note-off sends nothing more |
| `drop` | `true` drops the note |
| `techniques` | the playing techniques the expression map chooses from, as a set: `note.techniques.staccato`. Change it to choose another articulation (`note.techniques.legato = nil`); a list (`{ "staccato" }`) works too. Defaults the note had stay |
| `id`, `beat`, `dimensions` | read only: Dorico's note id, how strong its beat is (see Beats below), and Dorico's raw notation values |
| `note:cc(number, value, offset_ms, anchor)` | sends a controller with the note: `offset_ms` (default 0) from its start, or from its end with `anchor` `"end"` |

When a layer has several scripts, each sees the note as the one before left
it: start offsets and length changes add up, and the shortest
`max_length_ms` wins.

### Beats

`note.beat` comes from Dorico's metronome click (`docs/clicks.md`): 0 for a
note on a downbeat, 1 on another beat, 2 on a subdivision beat of a compound
meter (6/8's beats 2, 3, 5, 6), and 3 for anything else — in 4/4 an eighth
between beats is 3. Fiddle only knows the beats if Dorico sends it the click,
which takes two settings in each Dorico project:

1. In Play mode, route the **Tempo** track to Fiddle, port 1, channel 1
   (**Set Click Routing**). Install Playback Template can't do this: Dorico
   keeps it in the project, not in an endpoint configuration.
2. Turn the click on for playback (the metronome button in Play mode's
   toolbar). Routing alone sends nothing.

Without them every note's `beat` is 3. A Dorico project template saved with
both set gives every new project made from it the click
(`docs/clicks.md`).

### Controllers

`on_cc` sees each live controller on the layer's channel, before the
expression map does: `cc.number`, `cc.value` (0–127) and `cc.channel`
(1–16) can be changed, and `cc.drop = true` drops it.

### ctx

| Field | |
|---|---|
| `ctx.params` | the parameters' values (see Parameters), read only: a name the script doesn't declare is an error |
| `ctx.state` | a table for the script's own use. Fiddle empties it before every `on_reset`, so each pass starts the same |
| `ctx.harmony` | Fiddle's harmonic analysis: `key_root` (0–11, 0 is C), `is_minor`, `confidence`, `chord_root`, `chord_quality` (`"maj"`, `"min"`, `"dim"`, `"aug"`, `"dom7"`, `"maj7"`, `"min7"`, `"dim7"`, `"hdim7"`), `bass_root`, and for a note `is_diatonic` and `scale_degree` (1–7, 0 if chromatic) |
| `ctx.tempo_bpm` | Dorico's tempo |
| `ctx.time_ms` | when, on Fiddle's clock: only differences mean anything |

`on_reset` gets only `params` and `state`. It runs after the script loads
and at every playback start, bypassed or not, so a script turned back on
mid-pass finds itself set up.

### Examples

```lua
-- Never longer than 80 ms; shorter notes stay as they are.
function plugin.on_note(note, ctx)
  note.max_length_ms = 80
end

-- Accent the downbeats: louder, and marcato.
function plugin.on_note(note, ctx)
  if note.beat == 0 then
    note.velocity = note.velocity + 12
    note.techniques.marcato = true
  end
end
```

### Scripts for the earlier API

Scripts written for Fiddle's earlier API had `on_note_start`, `on_note_end`
and `on_load`. Such a script doesn't run: the layer plays without it, its card says
why, and it isn't offered under **Add Lua processor…**. When a version's copy
is one of these and the file has been updated (Fiddle's own scripts were),
**Use new file** on the card switches to the file and keeps the parameter
values.

## A version keeps its own copy

A file can change after a project is saved: the user edits it, or a Fiddle
update ships a new version of a bundled script. So a Fiddle version stores the
text of each script its layers run, next to the file name
(`StripBlob::luaPluginSources`, the `lua_sources` column of `strip_blobs`, and
of `strips` for the working setup). Restoring a version runs that saved copy
(`restoreLuaPlugin`), not the file, so the project plays the script as it was
saved, even if the file has changed or isn't on this Mac.

Adding a script reads its file, and saving keeps that text. Versions saved
before copies were kept have only the file name; they load the file as it is
now, and the restore banner says when that file has changed since the save
(`docs/dorico-project-identity-results.md`). Their next save keeps a copy. If
neither a copy nor the file is available, the layer keeps an unloaded
placeholder, so a later save doesn't drop the script from the layer.

## When the file has changed

Each script row in Layer details compares the copy with the file
(`LuaPlugin::fileStatus`, `ui/src/lib/luaScripts.js`):

- **changed**: the file differs from the copy. The layer keeps playing the copy,
  and **Use new file** switches every layer that uses that file name to the file
  as it is now (`UseLuaFileAction`, undoable). A file that fails to load
  changes nothing; the rows say why until the file changes again.
- **missing**: the file isn't on this Mac; the layer plays the copy.
- **unchanged**: nothing to say.

`fiddle_get_mixer` reports the same as `fileStatus` on each layer's
`luaProcessors`, with `enabled` and `params` (each parameter's declaration,
default and value).

## Random numbers repeat

A script that uses `math.random` (humanize.lua) plays the same on every pass.
Fiddle seeds each script per layer, from the layer ID and the file name
(`LuaPlugin::setSeed`), so two layers running one script don't jitter in step.
Whenever Dorico's transport starts, Fiddle reseeds the script and then calls its
`on_reset` (`LuaAnnotator::onTransportStart`). A pass from the same position
therefore repeats exactly, and reopening the project plays it the same way. A
pass started somewhere else draws different numbers for the same notes, since
the sequence starts at the first note played.

A script first runs with a seed from its file name. Joining a layer switches
it to the layer's seed, followed by `on_reset` as at a transport start, so
randomness drawn at load time (the script's top level or its first
`on_reset`) is the same on every layer. humanize.lua's **Variation** (its
`seed` parameter), when not Auto (0), replaces Fiddle's seed in `on_reset`,
so a new Variation plays from the next playback start.

## Parameters

A script declares its parameters in a `params` list, and Fiddle owns their
values (`LuaPluginMeta::Param`, `parseLuaParams`):

```lua
params = {
  { name = "timing_range_ms", label = "Timing", type = "float",
    min = 0, max = 50, step = 0.5, default = 15, unit = "ms",
    description = "Largest shift of a note's start" },
  { name = "on_beat_only", type = "bool", default = false },
  { name = "seed", label = "Variation", type = "int",
    min = 0, max = 99999, default = 0, control = "field",
    zero_label = "Auto", randomize = true },
  { name = "mode", type = "enum", options = { "soft", "hard" }, default = "soft" },
}
```

- `type` is `bool`, `int`, `float` or `enum`. `int` and `float` need `min`
  and `max`; `enum` needs `options`. `default` falls back to false, `min` or
  the first option.
- `label`, `unit`, `step`, `description` and `control` (`"knob"` or
  `"field"`, for numbers; a field also takes `zero_label` and `randomize`)
  only change how the control looks. Without a label,
  the name is shown with spaces.
- Parameters keep the order they're listed in.
- A parameter with a mistake (no range, a default outside it, a repeated name,
  an unknown type, no options) is skipped, and the problem is logged.

After the script's top-level code runs, and before its first `on_reset`,
Fiddle fills `ctx.params` with every parameter's value, so a script reads
`ctx.params.timing_range_ms` without a fallback. Fiddle only ever writes a
checked value there (`validLuaParamValue`): numbers are clamped to the range
and ints rounded, a bool must be true or false, and an enum must be one of
its options. A value the user set is kept only while it differs from the
default. When a script loads, saved values that no longer fit it are dropped
and logged; a script that doesn't load keeps them untouched.

A value changes through `setLuaParam` (one parameter), `resetLuaParams`
(all back to their defaults) and `setLuaEnabled` (bypass), each an Undo step
(`SetLuaParamsAction`, `SetLuaEnabledAction`); the steps of one knob drag
coalesce, like the gain fader's. The action holds the script instance, so a
step can't reach a different script after the chain changes. Values change
under the strip's MIDI-state lock, which the MIDI thread holds while it runs
scripts, and the script sees a new value from its next callback. The mixer
JSON gives each script `enabled`, `description`, `params`, `paramErrors` and
`paramNotes`.

### Saved with the version

A version keeps each script's bypass and the values the user set
(`LuaPlugin::savedState`, `StripBlob::luaPluginStates`, the `lua_states`
column of `strip_blobs` and `strips`, and `luaStates` in the state blob),
next to its saved copy. The text is canonical: values sorted by name, a
float as the 16 hex digits of its bits, so the same settings always hash the
same. A script enabled on its defaults saves nothing, so versions without
settings keep their hashes. Restoring applies the settings before the script
loads, so its first `on_reset` sees them. A value that doesn't fit the script any more
is dropped, and the script's row says so (`paramNotes`). A script that
doesn't load keeps its saved text exactly, so a later save doesn't lose it.

**Use new file** carries each layer's bypass and values over to the new
text, where it still declares them; the rows say which didn't carry over,
and Undo brings back the old script with all its values.

Bypass and Reset to defaults save the working setup at once, like mute; knob
changes are saved with the next save or on quit, like the gain fader.

### Controls

Every layer strip has a **MIDI FX** band above Edit VSTi, whether or not the
layer has scripts: scripts run on MIDI before the map and the instrument, so
the strip reads top to bottom in signal order, and a band of fixed height
(`--strip-midi-height`; a chair strip keeps a blank) keeps the faders lined
up. At the Compact and Comfortable sizes the band shows a count of scripts,
dimmed when all are bypassed, with an amber dot when one needs attention
(`midiFxSummary`); at Large it lists them, each with Bypass. An empty band
shows **+**.

Clicking the band opens the layer's MIDI FX panel (`MidiFxPanel.svelte`): its
scripts in chain order, and **Add script…** for the catalog. The panel floats
beside the strip and stays open while the user plays, solos or rides a fader;
it moves by its header, closes with × or Escape (never on an outside click),
and clicking another strip's band shows that layer. Opening it from a script's
row at Large shows that script's card.

Layer details and the panel show each script as a card (`LuaScriptCard.svelte`): its name,
a bypass switch, and a row per parameter (`LuaParamControls.svelte`). A
`bool` is a switch; an `int` or `float` is a knob with its value, which can
be typed (double-click the knob for the default); `control = "field"` is a
number field, showing `zero_label` for 0 and, with `randomize = true`, a
**New** button that picks a random whole value; an `enum` is a row of choices,
or a menu beyond four. A blue dot marks a changed value; clicking it resets
that one, and **Reset to defaults** resets them all. The card also shows the
script's declaration mistakes, values that didn't carry over, and its file
status with **Use new file**. A script that didn't load says why
(`loadError` in the mixer JSON).
