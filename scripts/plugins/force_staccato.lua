-- force_staccato.lua — Test / Debug Fiddle Lua script
--
-- No note lasts longer than the target length; shorter notes are left as
-- they are. Fiddle ends a longer note at the target, and Dorico's own
-- note-off then sends nothing more.

local plugin = {
  name        = "Force Staccato",
  version     = "4.0",
  author      = "Fiddle",
  description = "Ends every note by the target length (test/debug script).",

  params = {
    { name = "target_ms", label = "Target length", type = "float", min = 20, max = 500, step = 5,
      default = 80, unit = "ms", description = "The longest any note lasts" },
  },
}

function plugin.on_note(note, ctx)
  note.max_length_ms = ctx.params.target_ms
end

return plugin
