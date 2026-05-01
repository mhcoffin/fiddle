-- force_staccato.lua — Test / Debug Fiddle Lua plugin
--
-- Shortens every note to a target duration by scheduling an early
-- note-off at note-on time. This works regardless of when Dorico
-- sends its own note-off.
--
-- Configuration:
--   target_ms   – desired note duration in ms (default 80)

local plugin = {
  name        = "Force Staccato",
  version     = "3.1",
  author      = "Fiddle",
  description = "Shortens every note to a target duration (test/debug plugin).",

  params = {
    { name = "target_ms", type = "float", min = 20, max = 500, default = 80 },
  },
}

function plugin.on_note_start(note, ctx)
  local cfg = plugin.config or {}
  local target = cfg.target_ms or 80

  -- Always schedule an early note-off at the target duration.
  -- duration_samples is 0 at note-on time (Dorico hasn't set it yet),
  -- so we can't check whether the note is already short.
  -- If the note IS naturally shorter, Dorico's note-off arrives first
  -- and the scheduled one becomes a harmless duplicate.
  note.schedule_note_off_ms = target
  print(string.format("Staccato: scheduling noteOff at +%.0fms", target))

  return note
end

return plugin
