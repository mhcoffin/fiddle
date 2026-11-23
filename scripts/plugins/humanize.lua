-- humanize.lua — Fiddle's reference Lua script
--
-- Small random changes to each note's velocity, start and length, so that
-- programmed parts feel played.
--
-- Fiddle seeds math.random for each layer and starts it over whenever
-- playback starts, so every pass plays the same changes
-- (docs/lua-scripts.md).

local plugin = {
  name        = "Humanize",
  version     = "2.0",
  author      = "Fiddle",
  description = "Adds small random changes to each note's velocity, timing and length.",

  params = {
    { name = "velocity_range", label = "Velocity", type = "int", min = 0, max = 40, default = 8,
      description = "Largest change in a note's velocity, up or down" },
    { name = "timing_range_ms", label = "Timing", type = "float", min = 0, max = 50, step = 0.5,
      default = 15, unit = "ms", description = "Largest change in when a note starts, earlier or later" },
    { name = "length_range_ms", label = "Length", type = "float", min = 0, max = 50, step = 0.5,
      default = 10, unit = "ms", description = "Largest change in a note's length, longer or shorter" },
    { name = "seed", label = "Variation", type = "int", min = 0, max = 99999, default = 0,
      control = "field", zero_label = "Auto", randomize = true,
      description = "Which random sequence plays. Auto gives each layer its own; it changes from the next playback start." },
  },
}

-- A random number from -range to +range. Always draws, so changing one
-- control doesn't change the others' sequences.
local function jitter(range)
  return (math.random() * 2 - 1) * range
end

-- Variation: 0 (Auto) keeps Fiddle's seed for the layer; any other number
-- plays its own sequence.
function plugin.on_reset(ctx)
  if ctx.params.seed ~= 0 then math.randomseed(ctx.params.seed) end
end

function plugin.on_note(note, ctx)
  local p = ctx.params
  -- Fiddle keeps the velocity within 1–127.
  note.velocity = note.velocity + math.floor(jitter(p.velocity_range) + 0.5)
  note.start_offset_ms = jitter(p.timing_range_ms)
  note.length_delta_ms = jitter(p.length_range_ms)
end

return plugin
