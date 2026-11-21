-- humanize.lua — Reference Fiddle Lua plugin
--
-- Adds subtle, musically-aware randomisation to velocity, timing, and
-- note length so that programmed parts feel more "played."
--
-- Parameters (set in the layer's MIDI FX panel; Fiddle puts their values
-- in plugin.config, docs/lua-scripts.md):
--   velocity_range   – max ± velocity jitter (default 8)
--   timing_range_ms  – max ± onset shift in ms (default 15)
--   length_range_ms  – max ± duration jitter in ms (default 10)
--   seed             – "Variation": 0 (Auto) is Fiddle's seed for the layer;
--                      any other number plays a different fixed sequence,
--                      from the next playback start
--
-- Fiddle seeds math.random for each layer and reseeds it whenever playback
-- starts, so every pass plays the same jitter (docs/lua-scripts.md).

local plugin = {
  name        = "Humanize",
  version     = "1.1",
  author      = "Fiddle",
  description = "Adds subtle velocity, timing, and duration randomisation.",

  params = {
    { name = "velocity_range", label = "Velocity", type = "int", min = 0, max = 40, default = 8,
      description = "Largest change in a note's velocity, up or down" },
    { name = "timing_range_ms", label = "Timing", type = "float", min = 0, max = 50, step = 0.5,
      default = 15, unit = "ms", description = "How much note starts vary" },
    { name = "length_range_ms", label = "Length", type = "float", min = 0, max = 50, step = 0.5,
      default = 10, unit = "ms", description = "Largest change in a note's length, longer or shorter" },
    { name = "seed", label = "Variation", type = "int", min = 0, max = 99999, default = 0,
      control = "field", zero_label = "Auto", randomize = true,
      description = "Which random sequence plays. Auto gives each layer its own; it changes from the next playback start." },
  },
}

-- ── helpers ──────────────────────────────────────────────────────────

--- Return a random float in [-range, +range].
local function jitter(range)
  if range <= 0 then return 0 end
  return (math.random() * 2 - 1) * range
end

--- Clamp a value between lo and hi.
local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

-- ── lifecycle ────────────────────────────────────────────────────────

-- A seed set in the config replaces Fiddle's; 0 keeps Fiddle's.
local function apply_config_seed()
  local seed = plugin.config.seed
  if seed ~= 0 then math.randomseed(seed) end
end

function plugin.on_load(ctx)
  apply_config_seed()
end

-- ── note callbacks ───────────────────────────────────────────────────

function plugin.on_note_start(note, ctx)
  local cfg = plugin.config -- Fiddle fills in every parameter, checked
  local vel_range    = cfg.velocity_range
  local timing_range = cfg.timing_range_ms
  local length_range = cfg.length_range_ms

  -- 1. Velocity jitter
  if vel_range > 0 then
    local dv = math.floor(jitter(vel_range) + 0.5)
    note.velocity = clamp(note.velocity + dv, 1, 127)
  end

  -- 2. Timing jitter (shift the onset via a pre-note delay)
  if timing_range > 0 then
    local dt = jitter(timing_range)
    -- Only positive delays are realistically useful (can't go back in time).
    -- Map [-range, +range] → [0, 2*range] with centre at range.
    local delay_ms = clamp(math.floor(dt + timing_range + 0.5), 0, 2 * timing_range)
    if delay_ms > 0 then
      -- Insert a no-op CC with the desired delay so the note onset shifts.
      -- Using a CC 126 (Mono Mode) value 0, which is harmless and typically
      -- ignored by sample libraries.
      table.insert(note.pre_note, {
        type     = MIDI_CC,
        param1   = 126,    -- controller number
        param2   = 0,      -- value
        delay_ms = delay_ms,
      })
    end
  end

  -- 3. Duration jitter (via note length adjustment)
  --    Convert ms → samples, adjust duration_samples.
  if length_range > 0 and note.duration_samples and note.duration_samples > 0 then
    local sr = ctx.sample_rate or 44100
    local delta_samples = math.floor(jitter(length_range) * sr / 1000 + 0.5)
    -- Don't let duration go below ~10 ms
    local min_samples = math.floor(sr * 0.010)
    note.duration_samples = math.max(min_samples, note.duration_samples + delta_samples)
  end

  return note
end

function plugin.on_note_end(note, ctx)
  -- Nothing to do on note-off (could add release velocity jitter later).
  return note
end

function plugin.on_reset()
  -- Called as playback starts, after Fiddle has reseeded.
  apply_config_seed()
end

return plugin
