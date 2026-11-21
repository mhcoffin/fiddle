#pragma once

#include "TonalContext.h"
#include "midi_event.pb.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// sol2 requires these defines before inclusion
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace fiddle {

/// A script parameter's value: bool, int, float or enum (a string).
using LuaParamValue = std::variant<bool, std::int64_t, double, std::string>;

/// Metadata about a discovered Lua plugin (before instantiation).
struct LuaPluginMeta {
  std::string name;
  std::string version;
  std::string author;
  std::string description;
  std::string filePath; // absolute path to the .lua file

  /// Declared parameters, in the order the script lists them
  /// (docs/lua-scripts.md, "Parameters").
  struct Param {
    std::string name;
    std::string type; // "bool", "int", "float", "enum"
    std::string label; // shown; the name with spaces when not given
    std::string unit, description;
    std::string control = "knob"; // int/float: "knob" or "field"
    double min = 0, max = 0;
    double step = 0; // int: >= 1; float: 0 when the script didn't say
    LuaParamValue defaultValue;
    std::vector<std::string> options; // enum
  };
  std::vector<Param> params;
  /// Problems in the declaration. A parameter with one is skipped.
  std::vector<std::string> paramErrors;
};

/// Read a script's params table: ordered, checked, labels filled in.
void parseLuaParams(const sol::table &pluginTable, LuaPluginMeta &meta);

/// value as param accepts it: numbers clamped to min..max, ints rounded;
/// nullopt when it can't be (a string for a number, an enum value that isn't
/// one of the options, a NaN).
std::optional<LuaParamValue> validLuaParamValue(const LuaPluginMeta::Param &param,
                                                const LuaParamValue &value);

/// Context passed to Lua plugin callbacks. Populated by LuaAnnotator.
struct LuaContext {
  int stripChannel = 1;
  double sampleRate = 44100.0;
  double currentTimeMs = 0.0;
  std::string instrumentFamily;
  bool isSolo = true;

  /// Tonal context for the note being processed.
  /// Populated by MixerModel::routeAnnotatedNoteOn before the chain runs.
  TonalContext tonalContext;
};

/**
 * A single Lua plugin instance. Owns an isolated Lua VM (sol::state).
 *
 * Lifecycle:
 *   1. Construct with a .lua file path
 *   2. Call load() to parse and validate
 *   3. Call onNoteStart/onNoteEnd/onCC during playback
 *   4. Call reset() on transport start (reseeds, then on_reset)
 *
 * Thread safety: each instance has its own sol::state, so concurrent
 * calls to different instances are safe. Calls to the same instance
 * must be serialized (which the current pipeline architecture guarantees).
 */
class LuaPlugin {
public:
  explicit LuaPlugin(const std::string &filePath);
  ~LuaPlugin();

  /// Read the script file and run it. Returns true if the script loaded
  /// successfully and has valid callbacks. source() is then the file's text.
  bool load();

  /// Run a saved copy of the script instead of the file (a restored
  /// version's own copy, docs/lua-scripts.md). filePath() still names the
  /// file, for fileStatus() and display.
  bool loadSaved(const std::string &source);

  /// Reload the script from disk (hot-reload).
  bool reload();

  /// The text this instance runs: the file's text when it was loaded, or the
  /// saved copy. Empty when nothing was read (a placeholder for a missing file).
  const std::string &source() const { return source_; }

  /// How the file on disk compares with source(): unchanged, changed (the
  /// file was edited or replaced since this copy was read), or missing.
  enum class FileStatus { unchanged, changed, missing };
  FileStatus fileStatus() const;
  static const char *fileStatusName(FileStatus status);

  /// Why the file couldn't replace this copy ("Use new file"), until the file
  /// changes again; empty otherwise.
  const std::string &fileError() const { return fileError_; }
  void setFileError(std::string error) const { fileError_ = std::move(error); }

  /// The last error this instance logged (a script that failed to load).
  const std::string &lastError() const { return lastError_; }

  /// Whether the plugin loaded successfully.
  bool isLoaded() const { return loaded_; }

  /// Whether the plugin is enabled: off is bypassed, and the layer's notes
  /// pass through untouched. The MIDI thread reads it.
  bool isEnabled() const { return enabled_.load(std::memory_order_relaxed); }
  void setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }

  /// Plugin metadata (populated after load()).
  const LuaPluginMeta &meta() const { return meta_; }

  /// Parameters (docs/lua-scripts.md). Fiddle owns their values: before
  /// the script's on_load, plugin.config holds every declared parameter's
  /// value, and later changes write only checked values.
  ///
  /// The values the user set, by name; a parameter not listed has its
  /// default. Set them before load()/loadSaved() so on_load sees them. When
  /// the script loads, values that don't fit its declaration are dropped;
  /// when it doesn't, they're kept as they are, so a later save keeps them.
  void setParamOverrides(std::map<std::string, LuaParamValue> overrides);
  const std::map<std::string, LuaParamValue> &paramOverrides() const { return overrides_; }

  /// The declared parameter called name, or nullptr.
  const LuaPluginMeta::Param *findParam(const std::string &name) const;

  /// A parameter's current value: the user's, else the default.
  std::optional<LuaParamValue> paramValue(const std::string &name) const;

  /// Set a parameter, checked (validLuaParamValue). A value equal to the
  /// default removes the user's value. Returns false for an unknown name or a
  /// value that doesn't fit. The script sees it from its next callback; call
  /// with the strip's MIDI-state lock held, as the MIDI thread runs scripts
  /// under it.
  bool setParamValue(const std::string &name, const LuaParamValue &value);

  /// Make overrides the user's values for every parameter (the rest go back
  /// to their defaults), as setParamValue does one; same lock. For Undo and
  /// Reset to defaults.
  void setParamValues(const std::map<std::string, LuaParamValue> &overrides);

  /// Called for every note-on. Mutates the Note.
  /// Returns false if the plugin wants to suppress the note.
  bool onNoteStart(fiddle::Note &note, const LuaContext &ctx);

  /// Called on note-off. Mutates the Note.
  bool onNoteEnd(fiddle::Note &note, const LuaContext &ctx);

  /// Called for CC events. Returns true to forward, false to suppress.
  bool onCC(int ccNumber, int ccValue, int channel, const LuaContext &ctx);

  /// Reset plugin state when Dorico's transport starts: Fiddle reseeds the
  /// script's random numbers (setSeed), then calls its on_reset, so every
  /// pass plays the same (docs/lua-scripts.md).
  void reset();

  /// The seed for math.random. The default, derived from the file name,
  /// applies before the script first runs. A layer sets its own when the
  /// script joins it (MixerStrip), so two layers running one script differ;
  /// a new seed takes effect like a reset(), on_reset included. Every reset()
  /// reseeds.
  void setSeed(std::uint32_t seed);
  std::uint32_t seed() const { return seed_; }

  /// A stable 32-bit hash (FNV-1a) for seeds: the same on every run and Mac.
  static std::uint32_t seedFor(const std::string &text);

  /// Duration adjustment (ms) from the last onNoteEnd call.
  /// Negative = shorter, positive = longer.
  double lastDurationAdjustMs() const { return lastDurationAdjustMs_; }

  /// Scheduled note-off (ms after note-on) from the last onNoteStart call.
  /// Returns -1.0 if not set.
  double lastScheduledNoteOffMs() const { return lastScheduledNoteOffMs_; }

  /// Set a logging callback for plugin messages.
  void setLogCallback(std::function<void(const std::string &, bool)> cb) {
    logCallback_ = std::move(cb);
  }

  /// Get the file path.
  const std::string &filePath() const { return filePath_; }

private:
  std::string filePath_;
  std::string source_;
  std::uint32_t seed_ = 0;
  bool loaded_ = false;
  std::atomic<bool> enabled_{true};
  LuaPluginMeta meta_;
  std::map<std::string, LuaParamValue> overrides_;
  double lastDurationAdjustMs_ = 0.0;
  double lastScheduledNoteOffMs_ = -1.0;

  std::unique_ptr<sol::state> lua_;
  sol::table pluginTable_;

  // Cached function references (avoid repeated lookup)
  sol::optional<sol::protected_function> fnOnNoteStart_;
  sol::optional<sol::protected_function> fnOnNoteEnd_;
  sol::optional<sol::protected_function> fnOnCC_;
  sol::optional<sol::protected_function> fnOnReset_;
  sol::optional<sol::protected_function> fnOnLoad_;

  std::function<void(const std::string &, bool)> logCallback_;

  void log(const std::string &msg, bool isError = false);

  /// Run source_ in a fresh Lua state.
  bool run();

  /// math.randomseed(seed_) in the script's state.
  void reseed();

  /// overrides_ as the declared params accept them, without defaults; what
  /// doesn't fit is described in dropped.
  std::map<std::string, LuaParamValue> checkedOverrides(std::vector<std::string> &dropped) const;
  void writeConfig(const std::string &name, const LuaParamValue &value);

  // fileStatus() rereads the file only when its modification time changes.
  mutable long long checkedFileTime_ = -1;
  mutable FileStatus checkedStatus_ = FileStatus::missing;
  mutable std::string fileError_;
  std::string lastError_;

  /// Set up the Lua environment (sandbox, print redirect, etc.)
  void setupEnvironment();

  /// Register the Fiddle API types in the Lua state
  void registerAPI();

  /// Convert a Note protobuf → Lua table
  sol::table noteToLua(const fiddle::Note &note);

  /// Update a Note protobuf from a Lua table
  void luaToNote(const sol::table &t, fiddle::Note &note);

  /// Create a context table
  sol::table contextToLua(const LuaContext &ctx);

  /// Extract MidiInstructions from a Lua array
  void extractInstructions(const sol::table &luaList,
                           google::protobuf::RepeatedPtrField<
                               fiddle::MidiInstruction> *target);
};

/// A layer's script as a saved version names it: its saved copy when the
/// version has one, else the file (versions saved before copies were kept).
/// resolvedPath is the catalog's path for fileName, empty when it isn't on
/// this Mac. When neither runs, the result is an unloaded placeholder that
/// keeps the layer's reference, so a later save doesn't drop it.
std::shared_ptr<LuaPlugin> restoreLuaPlugin(const std::string &fileName,
                                            const std::string &source,
                                            const std::string &resolvedPath);

/// The saved copy of names[index] in a stored list of copies, or empty when
/// the list doesn't line up with the names (a version saved before copies
/// were kept, or a damaged record).
inline std::string savedLuaSource(const std::vector<std::string> &sources,
                                  const std::vector<std::string> &names, std::size_t index) {
  return sources.size() == names.size() && index < sources.size() ? sources[index] : std::string();
}

/**
 * Scans a directory for Lua plugin files and provides metadata.
 * Maintains an ordered search path for resolving plugin filenames
 * to absolute paths (for persistence — only filenames are stored).
 */
class LuaPluginCatalog {
public:
  /// Scan the default plugin directory.
  void scanDefaultDirectory();

  /// Scan a specific directory.
  void scanDirectory(const std::string &path);

  /// Get all discovered plugins.
  const std::vector<LuaPluginMeta> &plugins() const { return plugins_; }

  /// Find a plugin by absolute file path.
  const LuaPluginMeta *findByPath(const std::string &path) const;

  /// Find a plugin by filename only (e.g. "force_staccato.lua").
  /// Searches the catalog for a matching basename.
  const LuaPluginMeta *findByFileName(const std::string &fileName) const;

  /// Resolve a plugin filename (e.g. "force_staccato.lua") to an absolute
  /// path by searching the search path directories. Returns empty string
  /// if not found.
  std::string resolvePluginPath(const std::string &fileName) const;

  /// Get the current search path directories.
  const std::vector<std::string> &searchPaths() const { return searchPaths_; }

  /// Add a directory to the search path.
  void addSearchPath(const std::string &path) { searchPaths_.push_back(path); }

private:
  std::vector<LuaPluginMeta> plugins_;
  std::vector<std::string> searchPaths_; ///< Ordered plugin search directories.

  /// Probe a single .lua file for metadata without fully loading it.
  bool probePlugin(const std::string &filePath, LuaPluginMeta &meta);
};

} // namespace fiddle
