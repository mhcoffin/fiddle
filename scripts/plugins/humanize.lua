-- humanize.lua — Reference Fiddle Lua plugin
--
-- Adds subtle, musically-aware randomisation to velocity, timing, and
-- note length so that programmed parts feel more "played."
--
-- Configuration (via the plugin 'config' table):
--   velocity_range   – max ± velocity jitter (default 8)
--   timing_range_ms  – max ± onset shift in ms (default 15)
--   length_range_ms  – max ± duration jitter in ms (default 10)
--   seed             – PRNG seed; 0 = use os.clock (default 0)

local plugin = {
  name        = "Humanize",
  version     = "1.0",
  author      = "Fiddle",
  description = "Adds subtle velocity, timing, and duration randomisation.",

  params = {
    { name = "velocity_range", type = "int",   min = 0, max = 40, default = 8  },
    { name = "timing_range_ms", type = "float", min = 0, max = 50, default = 15 },
    { name = "length_range_ms", type = "float", min = 0, max = 50, default = 10 },
    { name = "seed",            type = "int",   min = 0, max = 99999, default = 0 },
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

function plugin.on_load(ctx)
  -- Seed the PRNG
  local cfg = plugin.config or {}
  local seed = cfg.seed or 0
  if seed == 0 then
    -- Use sample_rate as a cheap entropy source (os.clock is sandboxed out)
    seed = math.floor((ctx.sample_rate or 44100) * 1000 + math.random(1, 9999))
  end
  math.randomseed(seed)
  print("Humanize loaded, seed=" .. tostring(seed))
end

-- ── note callbacks ───────────────────────────────────────────────────

function plugin.on_note_start(note, ctx)
  local cfg = plugin.config or {}
  local vel_range    = cfg.velocity_range   or 8
  local timing_range = cfg.timing_range_ms  or 15
  local length_range = cfg.length_range_ms  or 10

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
  -- Re-seed on transport stop so each pass sounds slightly different.
  math.randomseed(math.random(1, 999999))
end

return plugin
