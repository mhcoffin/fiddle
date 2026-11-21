#include "LuaPlugin.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fiddle {

// ─────────────────────────────────────────────────────────────────────────────
// LuaPlugin implementation
// ─────────────────────────────────────────────────────────────────────────────

LuaPlugin::LuaPlugin(const std::string &filePath) : filePath_(filePath) {
  meta_.filePath = filePath_;
  const auto path = std::filesystem::path(filePath_);
  seed_ = seedFor(path.filename().string());
  meta_.name = path.stem().string();
  if (meta_.name.empty())
    meta_.name = path.filename().string();
}

LuaPlugin::~LuaPlugin() = default;

void LuaPlugin::log(const std::string &msg, bool isError) {
  if (isError)
    lastError_ = msg;
  if (logCallback_)
    logCallback_(msg, isError);
  else
    std::cerr << "[LuaPlugin] " << (isError ? "ERROR: " : "") << msg
              << std::endl;
}

void LuaPlugin::setupEnvironment() {
  // Open safe standard libraries (no os.execute, io.popen, etc.)
  lua_->open_libraries(sol::lib::base, sol::lib::string, sol::lib::table,
                       sol::lib::math, sol::lib::utf8);

  // Redirect print() to our logging system
  lua_->set_function("print", [this](sol::variadic_args va) {
    std::string msg;
    for (auto v : va) {
      if (!msg.empty())
        msg += "\t";
      msg += lua_->get<sol::function>("tostring")(v).get<std::string>();
    }
    log("[" + meta_.name + "] " + msg);
  });
}

void LuaPlugin::registerAPI() {
  // Register MidiInstruction type names as constants
  auto &lua = *lua_;
  lua["MIDI_CC"] = "cc";
  lua["MIDI_KEY_SWITCH"] = "key_switch";
  lua["MIDI_PROGRAM_CHANGE"] = "program_change";
  lua["MIDI_PITCH_BEND"] = "pitch_bend";
  lua["MIDI_CHANNEL_PRESSURE"] = "channel_pressure";

  // Chord quality constants (match fiddle::hmm::ChordQuality enum values)
  lua["CHORD_MAJ"]      = 0;
  lua["CHORD_MIN"]      = 1;
  lua["CHORD_DIM"]      = 2;
  lua["CHORD_AUG"]      = 3;
  lua["CHORD_DOM7"]     = 4;
  lua["CHORD_MAJ7"]     = 5;
  lua["CHORD_MIN7"]     = 6;
  lua["CHORD_DIM7"]     = 7;
  lua["CHORD_HALFDIM7"] = 8;
}

namespace {
std::optional<std::string> readText(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return std::nullopt;
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

long long fileTime(const std::string &path) {
  std::error_code ec;
  const auto time = std::filesystem::last_write_time(path, ec);
  return ec ? -1 : static_cast<long long>(time.time_since_epoch().count());
}
} // namespace

bool LuaPlugin::load() {
  loaded_ = false;
  source_.clear(); // until the file reads and runs
  auto text = readText(filePath_);
  if (!text) {
    log("Failed to load " + filePath_ + ": the file can't be read", true);
    return false;
  }
  source_ = std::move(*text);
  checkedFileTime_ = -1;
  if (run())
    return true;
  // A file that doesn't run isn't kept as the layer's copy: a later save
  // would otherwise store the broken text, and the fixed file would never be
  // read again. A saved copy (loadSaved) is kept even if it fails to run.
  source_.clear();
  return false;
}

bool LuaPlugin::loadSaved(const std::string &source) {
  source_ = source;
  checkedFileTime_ = -1;
  return run();
}

const char *LuaPlugin::fileStatusName(FileStatus status) {
  switch (status) {
  case FileStatus::unchanged: return "unchanged";
  case FileStatus::changed: return "changed";
  case FileStatus::missing: break;
  }
  return "missing";
}

LuaPlugin::FileStatus LuaPlugin::fileStatus() const {
  const auto time = std::filesystem::path(filePath_).is_absolute() ? fileTime(filePath_) : -1;
  if (time == -1)
    return FileStatus::missing;
  if (time != checkedFileTime_) {
    const auto text = readText(filePath_);
    checkedStatus_ = !text ? FileStatus::missing
                     : *text == source_ ? FileStatus::unchanged
                                        : FileStatus::changed;
    checkedFileTime_ = time;
    fileError_.clear();
  }
  return checkedStatus_;
}

bool LuaPlugin::run() {
  loaded_ = false;
  meta_.params.clear(); // until this source declares them
  meta_.paramErrors.clear();
  lua_ = std::make_unique<sol::state>();
  setupEnvironment();
  registerAPI();

  // Random numbers repeat from run to run (docs/lua-scripts.md).
  reseed();

  // The chunk is named after the file, so errors still say which script.
  const auto chunkName = "@" + std::filesystem::path(filePath_).filename().string();
  auto result = lua_->safe_script(source_, sol::script_pass_on_error, chunkName);
  if (!result.valid()) {
    sol::error err = result;
    log("Failed to load " + filePath_ + ": " + err.what(), true);
    return false;
  }

  // The script should return a table
  if (result.get_type() != sol::type::table) {
    log("Plugin script must return a table: " + filePath_, true);
    return false;
  }

  pluginTable_ = result.get<sol::table>();

  // Extract metadata
  meta_.filePath = filePath_;
  meta_.name = pluginTable_.get_or<std::string>("name", "Unnamed Plugin");
  meta_.version = pluginTable_.get_or<std::string>("version", "0.0");
  meta_.author = pluginTable_.get_or<std::string>("author", "Unknown");
  meta_.description =
      pluginTable_.get_or<std::string>("description", "No description");

  parseLuaParams(pluginTable_, meta_);
  for (const auto &error : meta_.paramErrors)
    log("[" + meta_.name + "] " + error, true);
  // The user's values reach plugin.config before on_load; overrides_ itself
  // changes only once the script has loaded, so a failure keeps it intact.
  std::vector<std::string> dropped;
  auto checked = checkedOverrides(dropped);
  for (const auto &param : meta_.params) {
    const auto found = checked.find(param.name);
    writeConfig(param.name, found != checked.end() ? found->second : param.defaultValue);
  }

  // Cache function references — only store if actually a function (sol::optional
  // from a nil Lua value still evaluates as "has value", causing call-nil errors)
  auto cacheFunction = [&](const char *name) -> sol::optional<sol::protected_function> {
    sol::object obj = pluginTable_[name];
    if (obj.valid() && obj.get_type() == sol::type::function)
      return obj.as<sol::protected_function>();
    return sol::nullopt;
  };
  fnOnNoteStart_ = cacheFunction("on_note_start");
  fnOnNoteEnd_ = cacheFunction("on_note_end");
  fnOnCC_ = cacheFunction("on_cc");
  fnOnReset_ = cacheFunction("on_reset");
  fnOnLoad_ = cacheFunction("on_load");

  // Call on_load if present (with a stub context for now)
  if (fnOnLoad_) {
    sol::table ctx = lua_->create_table();
    ctx["sample_rate"] = 44100.0;
    auto callResult = (*fnOnLoad_)(ctx);
    if (!callResult.valid()) {
      sol::error err = callResult;
      log("on_load error: " + std::string(err.what()), true);
      loaded_ = false;
      meta_.params.clear();
      meta_.paramErrors.clear();
      return false;
    }
  }

  overrides_ = std::move(checked);
  for (const auto &message : dropped)
    log("[" + meta_.name + "] " + message);
  loaded_ = true;
  log("Loaded plugin: " + meta_.name + " v" + meta_.version);
  return true;
}

bool LuaPlugin::reload() {
  log("Reloading plugin: " + meta_.name);
  return load();
}

void LuaPlugin::setParamOverrides(std::map<std::string, LuaParamValue> overrides) {
  overrides_ = std::move(overrides);
}

const LuaPluginMeta::Param *LuaPlugin::findParam(const std::string &name) const {
  for (const auto &param : meta_.params)
    if (param.name == name)
      return &param;
  return nullptr;
}

std::optional<LuaParamValue> LuaPlugin::paramValue(const std::string &name) const {
  const auto *param = findParam(name);
  if (!param)
    return std::nullopt;
  const auto found = overrides_.find(name);
  return found != overrides_.end() ? found->second : param->defaultValue;
}

bool LuaPlugin::setParamValue(const std::string &name, const LuaParamValue &value) {
  const auto *param = findParam(name);
  if (!param)
    return false;
  auto valid = validLuaParamValue(*param, value);
  if (!valid)
    return false;
  if (*valid == param->defaultValue)
    overrides_.erase(name);
  else
    overrides_[name] = *valid;
  if (loaded_)
    writeConfig(name, *valid);
  return true;
}

void LuaPlugin::writeConfig(const std::string &name, const LuaParamValue &value) {
  if (!lua_)
    return;
  sol::optional<sol::table> existing = pluginTable_["config"];
  sol::table config = existing ? *existing : lua_->create_table();
  if (!existing)
    pluginTable_["config"] = config;
  std::visit([&](const auto &v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, std::int64_t>)
      config[name] = static_cast<lua_Integer>(v);
    else
      config[name] = v;
  }, value);
}

std::map<std::string, LuaParamValue>
LuaPlugin::checkedOverrides(std::vector<std::string> &dropped) const {
  std::map<std::string, LuaParamValue> checked;
  for (const auto &[name, value] : overrides_) {
    const auto *param = findParam(name);
    auto valid = param ? validLuaParamValue(*param, value) : std::nullopt;
    if (!valid)
      dropped.push_back("Dropped the saved value of " + name +
                        (param ? ": it doesn't fit the parameter" : ": the script has no such parameter"));
    else if (*valid != param->defaultValue)
      checked.emplace(name, std::move(*valid));
  }
  return checked;
}

namespace {
std::string labelFor(const std::string &name) {
  std::string label = name;
  std::replace(label.begin(), label.end(), '_', ' ');
  if (!label.empty() && label[0] >= 'a' && label[0] <= 'z')
    label[0] = static_cast<char>(label[0] - 'a' + 'A');
  return label;
}

bool present(const sol::object &value) {
  return value.valid() && value.get_type() != sol::type::lua_nil && value.get_type() != sol::type::none;
}

std::optional<double> number(const sol::object &value) {
  if (value.get_type() != sol::type::number)
    return std::nullopt;
  const double v = value.as<double>();
  return std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
}
} // namespace

void parseLuaParams(const sol::table &pluginTable, LuaPluginMeta &meta) {
  meta.params.clear();
  meta.paramErrors.clear();
  sol::object declared = pluginTable["params"];
  if (!present(declared))
    return;
  if (declared.get_type() != sol::type::table) {
    meta.paramErrors.push_back("params must be a list");
    return;
  }
  const auto list = declared.as<sol::table>();
  const auto count = list.size();
  for (std::size_t i = 1; i <= count; ++i) {
    const auto where = "Parameter " + std::to_string(i);
    sol::object entry = list[i];
    if (entry.get_type() != sol::type::table) {
      meta.paramErrors.push_back(where + " isn't a table; skipped");
      continue;
    }
    const auto t = entry.as<sol::table>();
    LuaPluginMeta::Param p;
    p.name = t.get_or<std::string>("name", "");
    const auto fail = [&](const std::string &why) {
      meta.paramErrors.push_back((p.name.empty() ? where : p.name) + ": " + why + "; skipped");
    };
    if (p.name.empty()) { fail("it has no name"); continue; }
    if (std::any_of(meta.params.begin(), meta.params.end(),
                    [&](const auto &other) { return other.name == p.name; })) {
      fail("another parameter has this name");
      continue;
    }
    p.type = t.get_or<std::string>("type", "");
    sol::optional<std::string> label = t["label"];
    p.label = label && !label->empty() ? *label : labelFor(p.name);
    p.unit = t.get_or<std::string>("unit", "");
    p.description = t.get_or<std::string>("description", "");
    sol::object def = t["default"];
    const bool hasDefault = present(def);

    if (p.type == "bool") {
      if (hasDefault && def.get_type() != sol::type::boolean) { fail("its default isn't true or false"); continue; }
      p.defaultValue = hasDefault && def.as<bool>();
    } else if (p.type == "int" || p.type == "float") {
      const auto lo = number(t["min"]), hi = number(t["max"]);
      if (!lo || !hi) { fail("it needs a min and a max"); continue; }
      p.min = *lo;
      p.max = *hi;
      const bool isInt = p.type == "int";
      if (isInt) {
        // Whole numbers a double (and the UI's JavaScript) holds exactly.
        constexpr double exact = 9007199254740991.0;
        if (p.min < -exact || p.max > exact) { fail("its range is too large for whole numbers"); continue; }
        p.min = std::ceil(p.min);
        p.max = std::floor(p.max);
      }
      if (!(p.min < p.max)) { fail("its min isn't below its max"); continue; }
      sol::object stepValue = t["step"];
      const auto step = number(stepValue);
      if (present(stepValue) && (!step || *step <= 0)) { fail("its step isn't a positive number"); continue; }
      p.step = isInt ? std::max(1.0, std::round(step.value_or(1.0))) : step.value_or(0.0);
      const auto d = hasDefault ? number(def) : std::optional<double>(p.min);
      if (!d) { fail("its default isn't a number"); continue; }
      if (*d < p.min || *d > p.max) { fail("its default is outside min..max"); continue; }
      if (isInt)
        p.defaultValue = static_cast<std::int64_t>(std::llround(*d));
      else
        p.defaultValue = *d;
      p.control = t.get_or<std::string>("control", "knob");
      if (p.control != "knob" && p.control != "field") {
        meta.paramErrors.push_back(p.name + ": control must be \"knob\" or \"field\"; using a knob");
        p.control = "knob";
      }
    } else if (p.type == "enum") {
      sol::object options = t["options"];
      if (options.get_type() == sol::type::table) {
        const auto list = options.as<sol::table>();
        for (std::size_t o = 1; o <= list.size(); ++o) {
          sol::object option = list[o];
          if (option.get_type() == sol::type::string)
            p.options.push_back(option.as<std::string>());
        }
      }
      if (p.options.empty()) { fail("it needs a list of options"); continue; }
      std::vector<std::string> sorted = p.options;
      std::sort(sorted.begin(), sorted.end());
      if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) { fail("its options repeat"); continue; }
      if (hasDefault && (def.get_type() != sol::type::string ||
                         std::find(p.options.begin(), p.options.end(), def.as<std::string>()) == p.options.end())) {
        fail("its default isn't one of its options");
        continue;
      }
      p.defaultValue = hasDefault ? def.as<std::string>() : p.options.front();
    } else {
      fail("its type must be bool, int, float or enum");
      continue;
    }
    if (p.type != "int" && p.type != "float")
      p.control.clear();
    meta.params.push_back(std::move(p));
  }
}

std::optional<LuaParamValue> validLuaParamValue(const LuaPluginMeta::Param &param,
                                                const LuaParamValue &value) {
  if (param.type == "bool") {
    if (const auto *b = std::get_if<bool>(&value))
      return *b;
    return std::nullopt;
  }
  if (param.type == "enum") {
    const auto *s = std::get_if<std::string>(&value);
    if (s && std::find(param.options.begin(), param.options.end(), *s) != param.options.end())
      return *s;
    return std::nullopt;
  }
  std::optional<double> v;
  if (const auto *i = std::get_if<std::int64_t>(&value))
    v = static_cast<double>(*i);
  else if (const auto *d = std::get_if<double>(&value))
    v = *d;
  if (!v || !std::isfinite(*v))
    return std::nullopt;
  const double clamped = std::clamp(*v, param.min, param.max);
  if (param.type == "int")
    return static_cast<std::int64_t>(std::llround(clamped));
  if (param.type == "float")
    return clamped == 0.0 ? 0.0 : clamped; // no -0
  return std::nullopt;
}

sol::table LuaPlugin::noteToLua(const fiddle::Note &note) {
  auto &lua = *lua_;
  sol::table t = lua.create_table();

  t["id"] = note.id();
  t["note_number"] = (int)note.note_number();
  t["channel"] = (int)note.channel();
  t["velocity"] = (int)note.start_velocity();
  t["port"] = (int)note.port();
  t["duration_samples"] = note.duration_samples();

  // Dynamics mode
  t["dynamics_mode"] =
      (note.dynamics_mode() == fiddle::Note::CC) ? "cc" : "velocity";

  // Techniques map
  sol::table techs = lua.create_table();
  for (const auto &[dim, display] : note.notation_techniques())
    techs[dim] = display;
  t["techniques"] = techs;

  // Dimensions map
  sol::table dims = lua.create_table();
  for (const auto &[dim, val] : note.notation_dimensions())
    dims[dim] = val;
  t["dimensions"] = dims;

  // CC automation
  sol::table ccAuto = lua.create_table();
  for (const auto &[ccNum, lane] : note.cc_automation()) {
    sol::table points = lua.create_table();
    for (int i = 0; i < lane.points_size(); ++i) {
      sol::table pt = lua.create_table();
      pt["offset"] = lane.points(i).offset_samples();
      pt["value"] = (int)lane.points(i).value();
      points[i + 1] = pt; // 1-indexed for Lua
    }
    ccAuto[(int)ccNum] = points;
  }
  t["cc_automation"] = ccAuto;

  // Output instruction lists (start empty, plugins add to them)
  t["pre_note"] = lua.create_table();
  t["during_note"] = lua.create_table();
  t["post_note"] = lua.create_table();

  // Copy any existing instructions (from earlier plugins in the chain)
  for (const auto &instr : note.pre_note()) {
    sol::table entry = lua.create_table();
    switch (instr.type()) {
    case fiddle::MidiInstruction::CC:
      entry["type"] = "cc";
      break;
    case fiddle::MidiInstruction::KEY_SWITCH:
      entry["type"] = "key_switch";
      break;
    case fiddle::MidiInstruction::PROGRAM_CHANGE:
      entry["type"] = "program_change";
      break;
    case fiddle::MidiInstruction::PITCH_BEND:
      entry["type"] = "pitch_bend";
      break;
    case fiddle::MidiInstruction::CHANNEL_PRESSURE:
      entry["type"] = "channel_pressure";
      break;
    case fiddle::MidiInstruction::NOTE_ON:
      entry["type"] = "note_on";
      break;
    case fiddle::MidiInstruction::NOTE_OFF:
      entry["type"] = "note_off";
      break;
    default:
      break;
    }
    entry["param1"] = instr.param1();
    entry["param2"] = instr.param2();
    entry["delay_ms"] = instr.delay_ms();
    if (instr.channel() > 0)
      entry["channel"] = instr.channel();
    t.get<sol::table>("pre_note").add(entry);
  }

  return t;
}

void LuaPlugin::luaToNote(const sol::table &t, fiddle::Note &note) {
  // Read back mutable fields
  sol::optional<int> noteNum = t["note_number"];
  if (noteNum)
    note.set_note_number(std::clamp(*noteNum, 0, 127));

  sol::optional<int> vel = t["velocity"];
  if (vel)
    note.set_start_velocity(std::clamp(*vel, 1, 127));

  sol::optional<int> channel = t["channel"];
  if (channel)
    note.set_channel(*channel);

  sol::optional<uint64_t> durSamples = t["duration_samples"];
  if (durSamples)
    note.set_duration_samples(*durSamples);

  // Read back MIDI instructions
  sol::optional<sol::table> preNote = t["pre_note"];
  if (preNote) {
    note.clear_pre_note();
    extractInstructions(*preNote, note.mutable_pre_note());
  }

  sol::optional<sol::table> duringNote = t["during_note"];
  if (duringNote) {
    note.clear_during_note();
    extractInstructions(*duringNote, note.mutable_during_note());
  }

  sol::optional<sol::table> postNote = t["post_note"];
  if (postNote) {
    note.clear_post_note();
    extractInstructions(*postNote, note.mutable_post_note());
  }

  // Read back CC automation changes
  sol::optional<sol::table> ccAuto = t["cc_automation"];
  if (ccAuto) {
    note.clear_cc_automation();
    for (auto &kv : *ccAuto) {
      if (kv.first.get_type() == sol::type::number &&
          kv.second.get_type() == sol::type::table) {
        int ccNum = kv.first.as<int>();
        sol::table points = kv.second.as<sol::table>();
        auto &lane = (*note.mutable_cc_automation())[ccNum];
        for (auto &ptKv : points) {
          if (ptKv.second.get_type() == sol::type::table) {
            sol::table pt = ptKv.second.as<sol::table>();
            auto *point = lane.add_points();
            point->set_offset_samples(pt.get_or("offset", uint64_t{0}));
            point->set_value(pt.get_or("value", uint32_t{0}));
          }
        }
      }
    }
  }
}

sol::table LuaPlugin::contextToLua(const LuaContext &ctx) {
  auto &lua = *lua_;
  sol::table t = lua.create_table();
  t["strip_channel"] = ctx.stripChannel;
  t["sample_rate"] = ctx.sampleRate;
  t["current_time_ms"] = ctx.currentTimeMs;
  t["instrument_family"] = ctx.instrumentFamily;
  t["is_solo"] = ctx.isSolo;

  // Tonal context — populated by TonalCenterClassifier before the chain runs.
  // Lua API:
  //   ctx.tonal_context.key_root      -- int 0–11 (0=C)
  //   ctx.tonal_context.is_minor      -- bool
  //   ctx.tonal_context.confidence    -- float (Pearson r, 0–1)
  //   ctx.tonal_context.is_diatonic   -- bool
  //   ctx.tonal_context.scale_degree  -- int 1–7 (0 if chromatic)
  {
    sol::table tc = lua.create_table();
    tc["key_root"]     = ctx.tonalContext.current_key_root;
    tc["is_minor"]     = ctx.tonalContext.is_minor;
    tc["confidence"]   = ctx.tonalContext.confidence;
    tc["is_diatonic"]  = ctx.tonalContext.is_diatonic;
    tc["scale_degree"] = ctx.tonalContext.scale_degree;
    tc["chord_root"]   = ctx.tonalContext.chord_root;
    tc["chord_quality"] = ctx.tonalContext.chord_quality;
    tc["bass_pc"]      = ctx.tonalContext.bass_pitch_class;
    t["tonal_context"] = tc;
  }

  // Per-instance state table (persists across calls)
  sol::optional<sol::table> existingState = pluginTable_["_state"];
  if (!existingState) {
    pluginTable_["_state"] = lua.create_table();
  }
  t["state"] = pluginTable_["_state"];

  return t;
}

void LuaPlugin::extractInstructions(
    const sol::table &luaList,
    google::protobuf::RepeatedPtrField<fiddle::MidiInstruction> *target) {
  if (!target)
    return;
  for (auto &kv : luaList) {
    if (kv.second.get_type() != sol::type::table)
      continue;
    sol::table entry = kv.second.as<sol::table>();
    auto *instr = target->Add();

    std::string type = entry.get_or<std::string>("type", "cc");
    if (type == "cc")
      instr->set_type(fiddle::MidiInstruction::CC);
    else if (type == "key_switch")
      instr->set_type(fiddle::MidiInstruction::KEY_SWITCH);
    else if (type == "program_change")
      instr->set_type(fiddle::MidiInstruction::PROGRAM_CHANGE);
    else if (type == "pitch_bend")
      instr->set_type(fiddle::MidiInstruction::PITCH_BEND);
    else if (type == "channel_pressure")
      instr->set_type(fiddle::MidiInstruction::CHANNEL_PRESSURE);

    instr->set_param1(entry.get_or("param1", int{0}));
    instr->set_param2(entry.get_or("param2", int{0}));
    instr->set_delay_ms(entry.get_or("delay_ms", int{0}));
    instr->set_channel(entry.get_or("channel", int{0}));
  }
}

bool LuaPlugin::onNoteStart(fiddle::Note &note, const LuaContext &ctx) {
  lastScheduledNoteOffMs_ = -1.0; // reset each note
  if (!loaded_ || !enabled_ || !fnOnNoteStart_)
    return true;

  sol::table luaNote = noteToLua(note);
  sol::table luaCtx = contextToLua(ctx);

  auto result = (*fnOnNoteStart_)(luaNote, luaCtx);
  if (!result.valid()) {
    sol::error err = result;
    log("on_note_start error: " + std::string(err.what()), true);
    return true; // don't suppress on error
  }

  // If the plugin returned nil, suppress the note
  sol::object retval = result.get<sol::object>();
  if (retval.get_type() == sol::type::lua_nil)
    return false;

  // Read back the note table (returned or mutated in-place)
  sol::table resultTable = (retval.get_type() == sol::type::table)
                               ? retval.as<sol::table>()
                               : luaNote;
  luaToNote(resultTable, note);

  // Read back schedule_note_off_ms if set by the plugin
  sol::optional<double> noteOffMs = resultTable["schedule_note_off_ms"];
  if (noteOffMs && *noteOffMs > 0)
    lastScheduledNoteOffMs_ = *noteOffMs;

  return true;
}

bool LuaPlugin::onNoteEnd(fiddle::Note &note, const LuaContext &ctx) {
  lastDurationAdjustMs_ = 0.0; // reset each note
  if (!loaded_ || !enabled_ || !fnOnNoteEnd_)
    return true;

  sol::table luaNote = noteToLua(note);
  sol::table luaCtx = contextToLua(ctx);

  auto result = (*fnOnNoteEnd_)(luaNote, luaCtx);
  if (!result.valid()) {
    sol::error err = result;
    log("on_note_end error: " + std::string(err.what()), true);
    return true;
  }

  sol::object retval = result.get<sol::object>();
  if (retval.get_type() == sol::type::lua_nil)
    return false;

  // Read back the note table (returned or mutated in-place)
  sol::table resultTable = (retval.get_type() == sol::type::table)
                               ? retval.as<sol::table>()
                               : luaNote;
  luaToNote(resultTable, note);

  // Read back duration_adjust_ms if set by the plugin
  sol::optional<double> durAdjust = resultTable["duration_adjust_ms"];
  if (durAdjust)
    lastDurationAdjustMs_ = *durAdjust;

  return true;
}

bool LuaPlugin::onCC(int ccNumber, int ccValue, int channel,
                     const LuaContext &ctx) {
  if (!loaded_ || !enabled_ || !fnOnCC_)
    return true;

  sol::table luaCtx = contextToLua(ctx);
  auto result = (*fnOnCC_)(ccNumber, ccValue, channel, luaCtx);
  if (!result.valid()) {
    sol::error err = result;
    log("on_cc error: " + std::string(err.what()), true);
    return true;
  }

  sol::object retval = result.get<sol::object>();
  if (retval.get_type() == sol::type::boolean)
    return retval.as<bool>();

  return true; // default: forward
}

std::uint32_t LuaPlugin::seedFor(const std::string &text) {
  std::uint32_t hash = 2166136261u;
  for (const unsigned char c : text) {
    hash ^= c;
    hash *= 16777619u;
  }
  return hash;
}

void LuaPlugin::reseed() {
  if (!lua_)
    return;
  sol::optional<sol::protected_function> randomseed = (*lua_)["math"]["randomseed"];
  if (randomseed)
    (void)(*randomseed)(static_cast<lua_Integer>(seed_));
}

void LuaPlugin::setSeed(std::uint32_t seed) {
  if (seed == seed_)
    return;
  seed_ = seed;
  // As at a transport start: reseed, then on_reset, so a script that sets
  // its own seed there (humanize.lua's seed parameter) keeps it.
  reset();
}

void LuaPlugin::reset() {
  if (!loaded_)
    return;
  reseed();
  if (!enabled_ || !fnOnReset_)
    return;

  auto result = (*fnOnReset_)();
  if (!result.valid()) {
    sol::error err = result;
    log("on_reset error: " + std::string(err.what()), true);
  }
}

std::shared_ptr<LuaPlugin> restoreLuaPlugin(const std::string &fileName,
                                            const std::string &source,
                                            const std::string &resolvedPath) {
  auto plugin = std::make_shared<LuaPlugin>(resolvedPath.empty() ? fileName : resolvedPath);
  if (!source.empty())
    (void)plugin->loadSaved(source);
  else if (!resolvedPath.empty())
    (void)plugin->load();
  return plugin;
}

// ─────────────────────────────────────────────────────────────────────────────
// LuaPluginCatalog implementation
// ─────────────────────────────────────────────────────────────────────────────

void LuaPluginCatalog::scanDefaultDirectory() {
  // Default plugin directory: ~/Library/Application Support/Fiddle/plugins/
  std::string homeDir;
#if defined(__APPLE__)
  if (const char *home = std::getenv("HOME"))
    homeDir = std::string(home) +
              "/Library/Application Support/Fiddle/plugins";
#else
  homeDir = "./plugins";
#endif
  scanDirectory(homeDir);
}

void LuaPluginCatalog::scanDirectory(const std::string &path) {
  namespace fs = std::filesystem;

  // Register this directory in the search path (for resolvePluginPath)
  searchPaths_.push_back(path);

  if (!fs::exists(path)) {
    // Create the directory if it doesn't exist
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
      std::cerr << "[LuaPluginCatalog] Failed to create plugin directory: "
                << path << " (" << ec.message() << ")" << std::endl;
    }
    return;
  }

  for (const auto &entry : fs::directory_iterator(path)) {
    if (entry.is_regular_file() && entry.path().extension() == ".lua") {
      LuaPluginMeta meta;
      if (probePlugin(entry.path().string(), meta))
        plugins_.push_back(std::move(meta));
    } else if (entry.is_directory()) {
      // Check for init.lua inside subdirectory
      auto initLua = entry.path() / "init.lua";
      if (fs::exists(initLua)) {
        LuaPluginMeta meta;
        if (probePlugin(initLua.string(), meta))
          plugins_.push_back(std::move(meta));
      }
    }
  }

  std::cerr << "[LuaPluginCatalog] Scanned " << path << ": "
            << plugins_.size() << " plugins found" << std::endl;
}

bool LuaPluginCatalog::probePlugin(const std::string &filePath,
                                   LuaPluginMeta &meta) {
  // Lightweight probe: create a Lua state, load the file, extract metadata
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table,
                     sol::lib::math);

  auto result = lua.safe_script_file(filePath, sol::script_pass_on_error);
  if (!result.valid()) {
    sol::error err = result;
    std::cerr << "[LuaPluginCatalog] Failed to probe " << filePath << ": "
              << err.what() << std::endl;
    return false;
  }

  if (result.get_type() != sol::type::table) {
    std::cerr << "[LuaPluginCatalog] " << filePath
              << " does not return a table" << std::endl;
    return false;
  }

  sol::table pluginTable = result.get<sol::table>();
  meta.filePath = filePath;
  meta.name = pluginTable.get_or<std::string>("name", "Unnamed Plugin");
  meta.version = pluginTable.get_or<std::string>("version", "0.0");
  meta.author = pluginTable.get_or<std::string>("author", "Unknown");
  meta.description =
      pluginTable.get_or<std::string>("description", "No description");
  parseLuaParams(pluginTable, meta);

  return true;
}

const LuaPluginMeta *
LuaPluginCatalog::findByPath(const std::string &path) const {
  for (const auto &p : plugins_) {
    if (p.filePath == path)
      return &p;
  }
  return nullptr;
}

const LuaPluginMeta *
LuaPluginCatalog::findByFileName(const std::string &fileName) const {
  namespace fs = std::filesystem;
  for (const auto &p : plugins_) {
    if (fs::path(p.filePath).filename().string() == fileName)
      return &p;
  }
  return nullptr;
}

std::string
LuaPluginCatalog::resolvePluginPath(const std::string &fileName) const {
  namespace fs = std::filesystem;
  // Catalog entries can live in nested folders (commonly package/init.lua),
  // which cannot be reconstructed by appending the basename to a search root.
  if (const auto *meta = findByFileName(fileName);
      meta && fs::exists(meta->filePath)) {
    return meta->filePath;
  }
  for (const auto &dir : searchPaths_) {
    auto candidate = fs::path(dir) / fileName;
    if (fs::exists(candidate))
      return candidate.string();
  }
  // Fallback: check if fileName is already absolute and exists
  if (fs::path(fileName).is_absolute() && fs::exists(fileName))
    return fileName;
  return {};
}

} // namespace fiddle
