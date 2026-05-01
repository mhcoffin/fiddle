-- harmonic_debug.lua — Debug Fiddle Lua plugin
--
-- Prints the HMM harmonic analysis context on every note-on and note-off.
-- Assign this to any mixer strip to see real-time chord detection in the
-- FiddleServer console.
--
-- Output format:
--   [Harmony] Key: C | Chord: Fmaj | Bass: F | Note: 65 F (deg 4, diatonic)

local plugin = {
  name        = "Harmonic Debug",
  version     = "1.0",
  author      = "Fiddle",
  description = "Prints HMM harmonic analysis on each note (for development).",
  params      = {},
}

-- Pitch-class names (1-indexed for Lua, PC 0 = index 1)
local pc_names = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}

-- Quality names matching ChordQuality enum (0-indexed → 1-indexed)
local quality_names = {
  [1] = "maj",    -- Major      = 0
  [2] = "min",    -- Minor      = 1
  [3] = "dim",    -- Diminished = 2
  [4] = "aug",    -- Augmented  = 3
  [5] = "dom7",   -- Dom7       = 4
  [6] = "maj7",   -- Maj7       = 5
  [7] = "min7",   -- Min7       = 6
  [8] = "dim7",   -- Dim7       = 7
  [9] = "hdim7",  -- HalfDim7   = 8
}

local function pc_name(pc)
  return pc_names[(pc % 12) + 1] or "?"
end

local function quality_name(q)
  return quality_names[q + 1] or "?"
end

local function format_harmony(note, ctx)
  local tc = ctx.tonal_context
  if not tc then return "[Harmony] (no tonal context)" end

  local key_str   = pc_name(tc.key_root) .. (tc.is_minor and "m" or "")
  local chord_str = pc_name(tc.chord_root) .. quality_name(tc.chord_quality)
  local bass_str  = pc_name(tc.bass_pc)
  local note_pc   = pc_name(note.note_number)
  local diatonic  = tc.is_diatonic and "diatonic" or "chromatic"

  return string.format(
    "[Harmony] Key: %-3s | Chord: %-7s | Bass: %-2s | Note: %d %s (deg %d, %s)",
    key_str, chord_str, bass_str,
    note.note_number, note_pc,
    tc.scale_degree, diatonic
  )
end

function plugin.on_note_start(note, ctx)
  print(format_harmony(note, ctx))
  return note
end

function plugin.on_note_end(note, ctx)
  -- Uncomment the next line if you also want note-off logging:
  -- print("[Harmony] OFF: " .. note.note_number .. " " .. pc_name(note.note_number))
  return note
end

return plugin
