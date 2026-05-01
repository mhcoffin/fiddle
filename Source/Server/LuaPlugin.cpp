#include "LuaPlugin.h"
#include <filesystem>

namespace fiddle {

// ─────────────────────────────────────────────────────────────────────────────
// LuaPlugin implementation
// ─────────────────────────────────────────────────────────────────────────────

LuaPlugin::LuaPlugin(const std::string &filePath) : filePath_(filePath) {}

LuaPlugin::~LuaPlugin() = default;

void LuaPlugin::log(const std::string &msg, bool isError) {
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

bool LuaPlugin::load() {
  loaded_ = false;
  lua_ = std::make_unique<sol::state>();
  setupEnvironment();
  registerAPI();

  // Load the script file
  auto result = lua_->safe_script_file(filePath_, sol::script_pass_on_error);
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

  // Extract params (optional)
  meta_.params.clear();
  sol::optional<sol::table> paramsOpt = pluginTable_["params"];
  if (paramsOpt) {
    for (auto &kv : *paramsOpt) {
      if (kv.second.get_type() == sol::type::table) {
        sol::table pt = kv.second.as<sol::table>();
        LuaPluginMeta::Param p;
        p.name = pt.get_or<std::string>("name", "");
        p.type = pt.get_or<std::string>("type", "int");
        p.min = pt.get_or("min", 0.0);
        p.max = pt.get_or("max", 127.0);
        p.defaultVal = pt.get_or("default", 0.0);
        sol::optional<sol::table> options = pt["options"];
        if (options) {
          for (auto &opt : *options) {
            if (opt.second.get_type() == sol::type::string)
              p.options.push_back(opt.second.as<std::string>());
          }
        }
        if (!p.name.empty())
          meta_.params.push_back(std::move(p));
      }
    }
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

  loaded_ = true;
  log("Loaded plugin: " + meta_.name + " v" + meta_.version);

  // Call on_load if present (with a stub context for now)
  if (fnOnLoad_) {
    sol::table ctx = lua_->create_table();
    ctx["sample_rate"] = 44100.0;
    auto callResult = (*fnOnLoad_)(ctx);
    if (!callResult.valid()) {
      sol::error err = callResult;
      log("on_load error: " + std::string(err.what()), true);
    }
  }

  return true;
}

bool LuaPlugin::reload() {
  log("Reloading plugin: " + meta_.name);
  return load();
}

void LuaPlugin::setParam(const std::string &name, sol::object value) {
  if (!loaded_ || !lua_)
    return;
  // Store in the plugin table under a 'config' sub-table
  sol::optional<sol::table> existing = pluginTable_["config"];
  sol::table config = existing ? *existing : lua_->create_table();
  if (!existing)
    pluginTable_["config"] = config;
  config[name] = value;
}

sol::object LuaPlugin::getParam(const std::string &name) const {
  if (!loaded_ || !lua_)
    return sol::lua_nil;
  sol::optional<sol::table> config = pluginTable_["config"];
  if (config)
    return (*config)[name];
  return sol::lua_nil;
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

void LuaPlugin::reset() {
  if (!loaded_ || !enabled_ || !fnOnReset_)
    return;

  auto result = (*fnOnReset_)();
  if (!result.valid()) {
    sol::error err = result;
    log("on_reset error: " + std::string(err.what()), true);
  }
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

