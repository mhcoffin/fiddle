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

## Not kept yet

A script's parameters (`params`) have no controls yet, so every script runs on
its defaults and nothing about them is saved.
