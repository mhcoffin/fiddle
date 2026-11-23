#include "LuaPlugin.h"
#include "NoteInspection.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

namespace {
// Fiddle's helpers, run in each script's state. The note and controller a
// callback gets are proxies: reading or writing a field they don't have is
// an error that names it, so a typo can't pass for a field nobody reads.
constexpr const char *kPrelude = R"lua(
local error, setmetatable, tostring, type = error, setmetatable, tostring, type
local floor, huge = math.floor, math.huge

local function proxy(kind, values, writable, readonly)
  return setmetatable({}, {
    __index = function(_, k)
      local v = values[k]
      if v ~= nil then return v end
      v = readonly[k]
      if v ~= nil then return v end
      if writable[k] then return nil end
      error(kind .. " has no field '" .. tostring(k) .. "'", 2)
    end,
    __newindex = function(_, k, v)
      if writable[k] then values[k] = v return end
      if readonly[k] ~= nil then error(kind .. "." .. tostring(k) .. " can't be changed", 2) end
      error(kind .. " has no field '" .. tostring(k) .. "'", 2)
    end,
    __metatable = false,
  })
end

local function check_midi(what, v)
  if type(v) ~= "number" or v ~= floor(v) or v < 0 or v > 127 then
    error(what .. " must be a whole number from 0 to 127", 3)
  end
end

local NOTE = { pitch = true, velocity = true, channel = true, start_offset_ms = true,
  length_delta_ms = true, max_length_ms = true, drop = true, techniques = true }

local function make_note(values, readonly)
  local controllers = {}
  local note
  readonly.cc = function(self, number, value, offset_ms, anchor)
    if self ~= note then error("call note:cc(...) with a colon", 2) end
    check_midi("note:cc's controller number", number)
    check_midi("note:cc's value", value)
    if offset_ms ~= nil and (type(offset_ms) ~= "number" or offset_ms ~= offset_ms
                             or offset_ms == huge or offset_ms == -huge) then
      error("note:cc's offset must be a number of milliseconds", 2)
    end
    if anchor ~= nil and anchor ~= "start" and anchor ~= "end" then
      error("note:cc's anchor must be \"start\" or \"end\"", 2)
    end
    controllers[#controllers + 1] = { number, value, offset_ms or 0, anchor == "end" }
  end
  note = proxy("note", values, NOTE, readonly)
  return note, controllers
end

local CONTROLLER = { number = true, value = true, channel = true, drop = true }

local function make_controller(values)
  return proxy("cc", values, CONTROLLER, {})
end

-- Read only: Fiddle sets the values (LuaPlugin::writeConfig).
local function make_params(values)
  return proxy("ctx.params", {}, {}, values)
end

return { note = make_note, controller = make_controller, params = make_params }
)lua";

// Chord qualities by TonalContext::chord_quality (hmm::ChordQuality).
constexpr std::array<const char *, 9> kChordQualities = {
    "maj", "min", "dim", "aug", "dom7", "maj7", "min7", "dim7", "hdim7"};

// Scripts name techniques without Dorico's "pt." prefix.
std::string techniqueName(const std::string &id) {
  return id.rfind("pt.", 0) == 0 ? id.substr(3) : id;
}
std::string techniqueIdFromName(const std::string &name) {
  return name.rfind("pt.", 0) == 0 ? name : "pt." + name;
}
} // namespace

bool LuaPlugin::registerAPI() {
  auto helpers = lua_->safe_script(kPrelude, sol::script_pass_on_error, "=fiddle");
  if (!helpers.valid() || helpers.get_type() != sol::type::table) {
    log("Fiddle's script helpers failed to load", true);
    return false;
  }
  sol::table table = helpers;
  makeNote_ = table["note"];
  makeController_ = table["controller"];
  makeParams_ = table["params"];
  return true;
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
  meta_.params.clear(); // until the file reads and runs
  meta_.paramErrors.clear();
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
  // The file counts as unchanged until it's edited: only then might it run.
  source_.clear();
  checkedFileTime_ = fileTime(filePath_);
  checkedStatus_ = FileStatus::unchanged;
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
  // References into the old state go before it does.
  fnOnNote_.reset();
  fnOnCC_.reset();
  fnOnReset_.reset();
  makeNote_ = sol::lua_nil;
  makeController_ = sol::lua_nil;
  makeParams_ = sol::lua_nil;
  paramsView_ = sol::object();
  params_ = sol::lua_nil;
  state_ = sol::lua_nil;
  pluginTable_ = sol::lua_nil;
  lua_ = std::make_unique<sol::state>();
  setupEnvironment();
  if (!registerAPI())
    return false;
  params_ = lua_->create_table();
  state_ = lua_->create_table();
  {
    auto view = makeParams_(params_);
    if (!view.valid())
      return false;
    paramsView_ = view.get<sol::object>();
  }

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

  // A script written for the earlier API (on_note_start and friends) would
  // run with its callbacks never called: say so instead.
  for (const char *old : {"on_note_start", "on_note_end", "on_load"}) {
    if (pluginTable_[old].get_type() == sol::type::function) {
      log(std::filesystem::path(filePath_).filename().string() +
              " was written for Fiddle's earlier script API (it uses " + old + ").",
          true);
      return false;
    }
  }

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
  // The user's values reach ctx.params before the first on_reset;
  // overrides_ itself changes only once the script has loaded, so a failure
  // keeps it intact.
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
  fnOnNote_ = cacheFunction("on_note");
  fnOnCC_ = cacheFunction("on_cc");
  fnOnReset_ = cacheFunction("on_reset");

  // The script starts as every pass will.
  if (!startOver()) {
    loaded_ = false;
    meta_.params.clear();
    meta_.paramErrors.clear();
    return false;
  }

  overrides_ = std::move(checked);
  for (const auto &message : dropped)
    log("[" + meta_.name + "] " + message);
  paramNotes_ = std::move(dropped);
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
  const auto *param = loaded_ ? findParam(name) : nullptr;
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

void LuaPlugin::setParamValues(const std::map<std::string, LuaParamValue> &overrides) {
  if (!loaded_)
    return;
  for (const auto &param : meta_.params) {
    const auto found = overrides.find(param.name);
    if (found == overrides.end() || !setParamValue(param.name, found->second))
      (void)setParamValue(param.name, param.defaultValue);
  }
}

void LuaPlugin::writeConfig(const std::string &name, const LuaParamValue &value) {
  if (!lua_)
    return;
  std::visit([&](const auto &v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, std::int64_t>)
      params_[name] = static_cast<lua_Integer>(v);
    else
      params_[name] = v;
  }, value);
}

// savedState(): "!" when bypassed, then each value the user set, sorted by
// name: <length>:<name>, a type letter and the value. b is 0 or 1; i is
// decimal and ends with ';'; f is the double's 16 hex digits (exact, and the
// same in every locale); s is <length>:<text>.
std::string LuaPlugin::savedState() const {
  std::string text = isEnabled() ? "" : "!";
  for (const auto &[name, value] : overrides_) {
    text += std::to_string(name.size()) + ":" + name;
    std::visit([&](const auto &v) {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, bool>) {
        text += v ? "b1" : "b0";
      } else if constexpr (std::is_same_v<T, std::int64_t>) {
        text += "i" + std::to_string(v) + ";";
      } else if constexpr (std::is_same_v<T, double>) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(bits));
        text += std::string("f") + hex;
      } else {
        text += "s" + std::to_string(v.size()) + ":" + v;
      }
    }, value);
  }
  return text;
}

bool LuaPlugin::restoreSavedState(const std::string &text) {
  std::size_t at = 0;
  const bool enabled = !(at < text.size() && text[at] == '!');
  if (!enabled)
    ++at;
  // <digits>:<that many bytes>
  const auto counted = [&](std::string &out) {
    const auto colon = text.find(':', at);
    if (colon == std::string::npos || colon == at || colon - at > 12)
      return false;
    std::size_t length = 0;
    for (auto i = at; i < colon; ++i) {
      if (text[i] < '0' || text[i] > '9')
        return false;
      length = length * 10 + std::size_t(text[i] - '0');
    }
    if (length > text.size() - colon - 1)
      return false;
    out = text.substr(colon + 1, length);
    at = colon + 1 + length;
    return true;
  };
  std::map<std::string, LuaParamValue> values;
  while (at < text.size()) {
    std::string name;
    if (!counted(name) || at >= text.size())
      return false;
    const char type = text[at++];
    if (type == 'b') {
      if (at >= text.size() || (text[at] != '0' && text[at] != '1'))
        return false;
      values[name] = text[at++] == '1';
    } else if (type == 'i') {
      const auto end = text.find(';', at);
      if (end == std::string::npos || end == at || end - at > 20)
        return false;
      errno = 0;
      char *stop = nullptr;
      const auto digits = text.substr(at, end - at);
      const long long v = std::strtoll(digits.c_str(), &stop, 10);
      if (errno != 0 || stop != digits.c_str() + digits.size())
        return false;
      values[name] = static_cast<std::int64_t>(v);
      at = end + 1;
    } else if (type == 'f') {
      if (text.size() - at < 16)
        return false;
      std::uint64_t bits = 0;
      for (std::size_t i = 0; i < 16; ++i) {
        const char c = text[at + i];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0)
          return false;
        bits = (bits << 4) | static_cast<std::uint64_t>(digit);
      }
      double v = 0;
      std::memcpy(&v, &bits, sizeof v);
      if (!std::isfinite(v))
        return false;
      values[name] = v;
      at += 16;
    } else if (type == 's') {
      std::string v;
      if (!counted(v))
        return false;
      values[name] = std::move(v);
    } else {
      return false;
    }
  }
  setEnabled(enabled);
  overrides_ = std::move(values);
  return true;
}

std::map<std::string, LuaParamValue>
LuaPlugin::checkedOverrides(std::vector<std::string> &dropped) const {
  std::map<std::string, LuaParamValue> checked;
  for (const auto &[name, value] : overrides_) {
    const auto *param = findParam(name);
    auto valid = param ? validLuaParamValue(*param, value) : std::nullopt;
    if (!valid)
      dropped.push_back(param ? param->label + " is back to its default: the saved value doesn't fit it"
                              : "The saved value of " + name + " was dropped: the script no longer has it");
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
      if (p.control == "field") {
        p.zeroLabel = t.get_or<std::string>("zero_label", "");
        p.randomize = t.get_or("randomize", false);
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

sol::table LuaPlugin::contextToLua(const LuaContext &ctx) {
  auto &lua = *lua_;
  sol::table t = lua.create_table();
  t["params"] = paramsView_;
  t["state"] = state_;
  t["time_ms"] = ctx.timeMs;
  t["tempo_bpm"] = ctx.tempoBpm;
  const auto &tonal = ctx.tonalContext;
  sol::table harmony = lua.create_table();
  harmony["key_root"] = tonal.current_key_root;
  harmony["is_minor"] = tonal.is_minor;
  harmony["confidence"] = tonal.confidence;
  harmony["is_diatonic"] = tonal.is_diatonic;
  harmony["scale_degree"] = tonal.scale_degree;
  harmony["chord_root"] = tonal.chord_root;
  harmony["chord_quality"] =
      tonal.chord_quality >= 0 && tonal.chord_quality < static_cast<int>(kChordQualities.size())
          ? kChordQualities[static_cast<size_t>(tonal.chord_quality)]
          : "maj";
  harmony["bass_root"] = tonal.bass_pitch_class;
  t["harmony"] = harmony;
  return t;
}

namespace {
// Reads what a script left in a field, checked. On a problem, describe it in
// problem and leave out untouched.
bool readNumber(const sol::table &values, const char *key, double &out, std::string &problem,
                const char *kind) {
  const sol::object value = values[key];
  if (value.get_type() != sol::type::number || !std::isfinite(value.as<double>())) {
    problem = std::string(kind) + "." + key + " must be a number";
    return false;
  }
  out = value.as<double>();
  return true;
}
bool readWhole(const sol::table &values, const char *key, int lo, int hi, int &out,
               std::string &problem, const char *kind) {
  double number = 0;
  if (!readNumber(values, key, number, problem, kind))
    return false;
  out = static_cast<int>(std::clamp(std::round(number), double(lo), double(hi)));
  return true;
}
bool readFlag(const sol::table &values, const char *key, bool &out, std::string &problem,
              const char *kind) {
  const sol::object value = values[key];
  if (value.get_type() == sol::type::lua_nil) {
    out = false;
    return true;
  }
  if (value.get_type() != sol::type::boolean) {
    problem = std::string(kind) + "." + key + " must be true or false";
    return false;
  }
  out = value.as<bool>();
  return true;
}
} // namespace

std::optional<LuaNoteChange> LuaPlugin::onNote(const fiddle::Note &note, const LuaContext &ctx) {
  if (!loaded_ || !fnOnNote_)
    return std::nullopt;
  auto &lua = *lua_;

  sol::table values = lua.create_table();
  values["pitch"] = static_cast<int>(note.note_number());
  values["velocity"] = static_cast<int>(note.start_velocity());
  values["channel"] = static_cast<int>(note.channel());
  values["start_offset_ms"] = 0.0;
  values["length_delta_ms"] = 0.0;
  values["drop"] = false;
  // The techniques the expression map will choose from.
  const auto before = collectInputTechniqueIds(note);
  sol::table techniques = lua.create_table();
  for (const auto &id : before)
    techniques[techniqueName(id)] = true;
  values["techniques"] = techniques;

  sol::table readonly = lua.create_table();
  readonly["id"] = static_cast<lua_Integer>(note.id());
  readonly["beat"] = static_cast<int>(note.beat_prominence());
  sol::table dimensions = lua.create_table();
  for (const auto &[dimension, value] : note.notation_dimensions())
    dimensions[dimension] = value;
  readonly["dimensions"] = dimensions;

  auto made = makeNote_(values, readonly);
  if (!made.valid()) {
    sol::error err = made;
    log("[" + meta_.name + "] " + err.what(), true);
    return std::nullopt;
  }
  sol::object proxy = made.get<sol::object>(0);
  sol::table controllers = made.get<sol::table>(1);

  auto result = (*fnOnNote_)(proxy, contextToLua(ctx));
  if (!result.valid()) {
    sol::error err = result;
    log("[" + meta_.name + "] on_note: " + err.what(), true);
    return std::nullopt;
  }

  // Everything is checked before anything is used: a note is changed
  // completely or not at all.
  LuaNoteChange change;
  std::string problem;
  double maxLength = 0;
  const sol::object cap = values["max_length_ms"];
  const sol::object chosen = values["techniques"];
  const bool ok =
      readFlag(values, "drop", change.drop, problem, "note") &&
      readWhole(values, "pitch", 0, 127, change.pitch, problem, "note") &&
      readWhole(values, "velocity", 1, 127, change.velocity, problem, "note") &&
      readWhole(values, "channel", 1, 16, change.channel, problem, "note") &&
      readNumber(values, "start_offset_ms", change.startOffsetMs, problem, "note") &&
      readNumber(values, "length_delta_ms", change.lengthDeltaMs, problem, "note") &&
      (cap.get_type() == sol::type::lua_nil ||
       readNumber(values, "max_length_ms", maxLength, problem, "note")) &&
      (chosen.get_type() == sol::type::table ||
       (problem = "note.techniques must be a table, like { staccato = true }", false));
  if (!ok) {
    log("[" + meta_.name + "] on_note: " + problem, true);
    return std::nullopt;
  }
  change.startOffsetMs = std::clamp(change.startOffsetMs, -kLuaMaxStartOffsetMs, kLuaMaxStartOffsetMs);
  change.lengthDeltaMs = std::clamp(change.lengthDeltaMs, -kLuaMaxLengthDeltaMs, kLuaMaxLengthDeltaMs);
  if (cap.get_type() != sol::type::lua_nil)
    change.maxLengthMs = std::max(maxLength, kLuaMinNoteLengthMs);

  // A set ({ staccato = true }) or a list ({ "staccato" }); no empty names.
  std::set<std::string> after;
  const auto choose = [&](const std::string &name) {
    if (!name.empty() && name != "pt.")
      after.insert(techniqueIdFromName(name));
  };
  for (const auto &[key, value] : chosen.as<sol::table>()) {
    if (key.get_type() == sol::type::string && value.get_type() != sol::type::lua_nil &&
        !(value.get_type() == sol::type::boolean && !value.as<bool>()))
      choose(key.as<std::string>());
    else if (key.get_type() == sol::type::number && value.get_type() == sol::type::string)
      choose(value.as<std::string>());
  }
  if (after != before)
    change.techniques = std::move(after);

  // In the order the script sent them.
  for (std::size_t i = 1, n = controllers.size(); i <= n; ++i) {
    const sol::table t = controllers[i];
    const double offset = t.get<double>(3);
    change.controllers.push_back({t.get<int>(1), t.get<int>(2),
                                  std::isfinite(offset)
                                      ? std::clamp(offset, -kLuaMaxLengthDeltaMs, kLuaMaxLengthDeltaMs)
                                      : 0.0,
                                  t.get<bool>(4)});
  }
  return change;
}

std::optional<LuaControllerChange> LuaPlugin::onCC(int number, int value, int channel,
                                                   const LuaContext &ctx) {
  if (!loaded_ || !fnOnCC_)
    return std::nullopt;
  sol::table values = lua_->create_table();
  values["number"] = number;
  values["value"] = value;
  values["channel"] = channel;
  values["drop"] = false;
  auto made = makeController_(values);
  if (!made.valid()) {
    sol::error err = made;
    log("[" + meta_.name + "] " + err.what(), true);
    return std::nullopt;
  }
  auto result = (*fnOnCC_)(made.get<sol::object>(), contextToLua(ctx));
  if (!result.valid()) {
    sol::error err = result;
    log("[" + meta_.name + "] on_cc: " + err.what(), true);
    return std::nullopt;
  }
  LuaControllerChange change;
  std::string problem;
  if (!(readFlag(values, "drop", change.drop, problem, "cc") &&
        readWhole(values, "number", 0, 127, change.number, problem, "cc") &&
        readWhole(values, "value", 0, 127, change.value, problem, "cc") &&
        readWhole(values, "channel", 1, 16, change.channel, problem, "cc"))) {
    log("[" + meta_.name + "] on_cc: " + problem, true);
    return std::nullopt;
  }
  return change;
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
  if (loaded_)
    (void)startOver();
}

bool LuaPlugin::startOver() {
  reseed();
  state_ = lua_->create_table();
  if (!fnOnReset_)
    return true;
  sol::table ctx = lua_->create_table();
  ctx["params"] = paramsView_;
  ctx["state"] = state_;
  auto result = (*fnOnReset_)(ctx);
  if (!result.valid()) {
    sol::error err = result;
    log("[" + meta_.name + "] on_reset: " + err.what(), true);
    return false;
  }
  return true;
}

std::shared_ptr<LuaPlugin> restoreLuaPlugin(const std::string &fileName,
                                            const std::string &source,
                                            const std::string &resolvedPath,
                                            const std::string &state) {
  auto plugin = std::make_shared<LuaPlugin>(resolvedPath.empty() ? fileName : resolvedPath);
  if (!state.empty() && !plugin->restoreSavedState(state))
    std::cerr << "[LuaPlugin] Ignored the damaged saved settings of " << fileName << std::endl;
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
  // A script for the earlier API wouldn't run (LuaPlugin::run), so it isn't
  // offered.
  for (const char *old : {"on_note_start", "on_note_end", "on_load"}) {
    if (pluginTable[old].get_type() == sol::type::function) {
      std::cerr << "[LuaPluginCatalog] " << filePath << " uses the earlier script API ("
                << old << "); not listed" << std::endl;
      return false;
    }
  }
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
