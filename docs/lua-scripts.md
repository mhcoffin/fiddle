# Lua scripts on layers

A layer can run Lua scripts ("Fiddles", `LuaPlugin.h`) before its expression
map. Scripts are files: Fiddle's own ship in the app bundle
(`scripts/plugins`), and the user's go in
`~/Library/Application Support/Fiddle/plugins`. Layer details lists the scripts
Fiddle found under **Add Lua processor…**.

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
`luaProcessors`.

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
randomness drawn at load time (the script's top level or `on_load`) is the
same on every layer. humanize.lua's `seed` parameter, when not 0, replaces
Fiddle's seed in `on_load` and `on_reset`.

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
    min = 0, max = 99999, default = 0, control = "field" },
  { name = "mode", type = "enum", options = { "soft", "hard" }, default = "soft" },
}
```

- `type` is `bool`, `int`, `float` or `enum`. `int` and `float` need `min`
  and `max`; `enum` needs `options`. `default` falls back to false, `min` or
  the first option.
- `label`, `unit`, `step`, `description` and `control` (`"knob"` or
  `"field"`, for numbers) only change how the control looks. Without a label,
  the name is shown with spaces.
- Parameters keep the order they're listed in.
- A parameter with a mistake (no range, a default outside it, a repeated name,
  an unknown type, no options) is skipped, and the problem is logged.

After the script's top-level code runs, and before its `on_load`, Fiddle
fills `plugin.config` with every parameter's value, so a script reads
`plugin.config.timing_range_ms` without a fallback. Fiddle only ever writes a
checked value there (`validLuaParamValue`): numbers are clamped to the range
and ints rounded, a bool must be true or false, and an enum must be one of
its options. A value the user set is kept only while it differs from the
default. When a script loads, saved values that no longer fit it are dropped
and logged; a script that doesn't load keeps them untouched.

There are no controls for parameters yet, and their values aren't saved, so
every script runs on its defaults.
