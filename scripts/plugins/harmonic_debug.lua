-- harmonic_debug.lua — Debug Fiddle Lua script
--
-- Prints Fiddle's harmonic analysis (ctx.harmony) for every note. Add it to
-- any layer to watch chord detection in the FiddleServer console.
--
-- Output format:
--   [Harmony] Key: C | Chord: Fmaj | Bass: F | Note: 65 F (deg 4, diatonic)

local plugin = {
  name        = "Harmonic Debug",
  version     = "2.0",
  author      = "Fiddle",
  description = "Prints Fiddle's harmonic analysis on each note (for development).",
}

local pc_names = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }

local function pc_name(pc)
  return pc_names[(pc % 12) + 1]
end

function plugin.on_note(note, ctx)
  local h = ctx.harmony
  print(string.format(
    "[Harmony] Key: %-3s | Chord: %-7s | Bass: %-2s | Note: %d %s (deg %d, %s)",
    pc_name(h.key_root) .. (h.is_minor and "m" or ""),
    pc_name(h.chord_root) .. h.chord_quality,
    pc_name(h.bass_root),
    note.pitch, pc_name(note.pitch),
    h.scale_degree, h.is_diatonic and "diatonic" or "chromatic"))
end

return plugin
