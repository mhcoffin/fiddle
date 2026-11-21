#pragma once

// Which of a hosted plug-in's state changes are the user's edits, so the
// project becomes unsaved, and which are the plug-in changing its own state:
// finishing loading samples after it opened, rewriting its state, or mirroring
// MIDI controllers from Dorico. A user edits a plug-in only in its editor
// (Fiddle's own commands on plug-ins mark the project themselves), so a change
// counts only with a clear editor gesture, or while the editor is open and for
// a short grace after it closes, which covers plug-ins that report a change late
// and the ~2 s parameter poll. Without this, an untouched project reopened
// later could look edited, and a Dorico save would make a new version or branch
// ("Main (2)") for it (docs/project-save-versioning.md).

#include <cstdint>

namespace fiddle {

inline constexpr uint32_t kPluginEditorGraceMs = 5000;

/// When a plug-in's editor was last seen open.
struct PluginEditorWatch {
  bool seen = false;
  uint32_t lastOpenMs = 0;

  void observe(bool visible, uint32_t nowMs) {
    if (visible) {
      seen = true;
      lastOpenMs = nowMs;
    }
  }
  /// Open now, or closed within the grace (millisecond counters wrap).
  bool recentlyOpen(uint32_t nowMs) const {
    return seen && uint32_t(nowMs - lastOpenMs) < kPluginEditorGraceMs;
  }
};

/// explicitEdit: the plug-in reported an editor gesture (a control dragged).
/// stateChanged: its parameters or its other state changed.
inline bool isUserPluginEdit(bool explicitEdit, bool stateChanged, bool editorRecentlyOpen) {
  return explicitEdit || (stateChanged && editorRecentlyOpen);
}

} // namespace fiddle
