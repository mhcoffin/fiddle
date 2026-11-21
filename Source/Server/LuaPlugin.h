#pragma once

#include "TonalContext.h"
#include "midi_event.pb.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// sol2 requires these defines before inclusion
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace fiddle {

/// Metadata about a discovered Lua plugin (before instantiation).
struct LuaPluginMeta {
  std::string name;
  std::string version;
  std::string author;
  std::string description;
  std::string filePath; // absolute path to the .lua file

  /// Declared configurable parameters.
  struct Param {
    std::string name;
    std::string type; // "int", "float", "bool", "enum"
    double min = 0, max = 127, defaultVal = 0;
    std::vector<std::string> options; // for enum type
  };
  std::vector<Param> params;
};

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
 *   4. Call reset() on transport stop
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

  /// Whether the plugin is enabled (can be toggled by user).
  bool isEnabled() const { return enabled_; }
  void setEnabled(bool enabled) { enabled_ = enabled; }

  /// Plugin metadata (populated after load()).
  const LuaPluginMeta &meta() const { return meta_; }

  /// Get/set a configuration parameter.
  void setParam(const std::string &name, sol::object value);
  sol::object getParam(const std::string &name) const;

  /// Called for every note-on. Mutates the Note.
  /// Returns false if the plugin wants to suppress the note.
  bool onNoteStart(fiddle::Note &note, const LuaContext &ctx);

  /// Called on note-off. Mutates the Note.
  bool onNoteEnd(fiddle::Note &note, const LuaContext &ctx);

  /// Called for CC events. Returns true to forward, false to suppress.
  bool onCC(int ccNumber, int ccValue, int channel, const LuaContext &ctx);

  /// Reset plugin state (transport stop).
  void reset();

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
  bool loaded_ = false;
  bool enabled_ = true;
  LuaPluginMeta meta_;
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
