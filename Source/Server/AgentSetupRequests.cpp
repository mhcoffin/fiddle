#include "AgentSetupRequests.h"

#include <algorithm>
#include <map>
#include <set>

namespace fiddle::agent {
namespace {

juce::StringArray words(const juce::String &text) {
  juce::StringArray result;
  result.addTokens(text.trim().toLowerCase(), " \t", "");
  result.removeEmptyStrings();
  return result;
}

juce::String compact(const juce::String &text) {
  juce::String result;
  for (const auto character : text.toLowerCase())
    if (juce::CharacterFunctions::isLetterOrDigit(character)) result += character;
  return result;
}

int relevance(const BrowsableInstrument &instrument, const juce::String &query) {
  const auto name = instrument.name.toLowerCase();
  if (query.isEmpty()) return 2;
  if (name == query) return 0;
  if (name.startsWith(query)) return 1;
  return 2;
}

std::optional<std::string> stringField(const juce::var &object, const char *name) {
  if (!object.hasProperty(name)) return std::nullopt;
  const auto value = object[name];
  if (value.isVoid() || value.isUndefined()) return std::nullopt;
  return value.toString().trim().toStdString();
}

std::optional<int> intField(const juce::var &object, const char *name) {
  if (!object.hasProperty(name)) return std::nullopt;
  const auto value = object[name];
  if (!value.isInt() && !value.isInt64()) return std::nullopt;
  return static_cast<int>(value);
}

LibraryEdit failure(juce::String error) {
  LibraryEdit edit;
  edit.error = std::move(error);
  return edit;
}

bool validCharacter(const std::string &character) {
  return character.empty() || character == "solo" || character == "section" ||
         character == "ensemble" || character == "overlay";
}

} // namespace

bool matchesSearch(const juce::String &text, const juce::String &query) {
  const auto terms = words(query);
  if (terms.isEmpty()) return true;
  juce::StringArray tokens;
  juce::String token;
  for (const auto character : text.toLowerCase() + " ") {
    if (juce::CharacterFunctions::isLetterOrDigit(character)) {
      token += character;
    } else if (token.isNotEmpty()) {
      tokens.add(token);
      token.clear();
    }
  }
  const auto whole = compact(text);
  return std::all_of(terms.begin(), terms.end(), [&](const juce::String &term) {
    const auto word = compact(term);
    if (word.isEmpty()) return true;
    if (std::any_of(tokens.begin(), tokens.end(),
                    [&](const juce::String &candidate) { return candidate.startsWith(word); }))
      return true;
    return word.length() >= 4 && whole.contains(word);
  });
}

std::vector<const BrowsableInstrument *>
searchInstruments(const std::vector<BrowsableInstrument> &instruments,
                  const juce::String &query, const juce::String &family,
                  bool includeVariants, int limit) {
  const auto terms = words(query);
  const auto wholeQuery = terms.joinIntoString(" ");
  const auto wantedFamily = family.trim().toLowerCase();
  std::vector<std::pair<std::size_t, const BrowsableInstrument *>> matches;
  for (std::size_t index = 0; index < instruments.size(); ++index) {
    const auto &instrument = instruments[index];
    if (!includeVariants && !instrument.isDefault) continue;
    if (wantedFamily.isNotEmpty() && instrument.family.toLowerCase() != wantedFamily) continue;
    if (matchesSearch(instrument.name + " " + instrument.entityID, query))
      matches.emplace_back(index, &instrument);
  }
  std::stable_sort(matches.begin(), matches.end(), [&](const auto &a, const auto &b) {
    const int ra = relevance(*a.second, wholeQuery), rb = relevance(*b.second, wholeQuery);
    if (ra != rb) return ra < rb;
    if (a.second->isDefault != b.second->isDefault) return a.second->isDefault;
    return a.first < b.first;
  });
  std::vector<const BrowsableInstrument *> result;
  for (const auto &match : matches) {
    if (static_cast<int>(result.size()) >= std::max(0, limit)) break;
    result.push_back(match.second);
  }
  return result;
}

bool coversInstrument(const std::vector<std::string> &listed, const std::string &instrument) {
  const auto canonical = [](const std::string &id) {
    const auto at = id.find(".alias.");
    return at == std::string::npos ? id : id.substr(0, at);
  };
  const auto wanted = canonical(instrument);
  return std::any_of(listed.begin(), listed.end(),
                     [&](const std::string &id) { return canonical(id) == wanted; });
}

std::string patchBaseline(const LibraryPatchRow &patch) {
  return std::to_string(patch.revision) + "|" + std::to_string(patch.pluginUid) + "|" +
         (patch.setupComplete ? "done" : "waiting") + "|" + patch.expectedPresetName;
}

PlayerCapture decidePlayerCapture(const PlayerSessionState &state,
                                  const std::string &requestedSession, bool allowUnchanged,
                                  const std::string &currentBaseline) {
  const bool named = !requestedSession.empty();
  if (named && requestedSession == state.lastCaptured)
    return PlayerCapture::alreadyCaptured;
  if (!state.open)
    return named ? PlayerCapture::stale : PlayerCapture::notOpen;
  if (named && requestedSession != state.session)
    return PlayerCapture::stale;
  if (state.status == "loading") return PlayerCapture::loading;
  if (state.status == "failed") return PlayerCapture::failed;
  if (currentBaseline != state.baseline) return PlayerCapture::patchChanged;
  if (!state.changed && !allowUnchanged) return PlayerCapture::unchanged;
  return PlayerCapture::capture;
}

BridgeCheck bridgeCheck(const juce::String &beacon, bool verified, bool othersReported,
                        int projectRevision, int assignedRevision, bool ambiguous) {
  if (beacon == "matched" && projectRevision > 0) {
    const juce::String age = assignedRevision > 0 && projectRevision > assignedRevision ? "a newer" : "an older";
    return {"playingProjectRevision",
            "This project in Dorico has " + age + " revision of the chair's bridge map (r" +
                juce::String(projectRevision) + "), which Fiddle installed before, so Fiddle plays it "
                "with that revision. Reapplying the Fiddle playback template in the project moves it "
                "to the assigned revision; nothing needs doing otherwise."};
  }
  if (beacon == "mismatched")
    return {"differentVersion",
            juce::String(ambiguous
                ? "Dorico's copy of this chair's bridge map matches more than one revision Fiddle installed, "
                  "so Fiddle can't tell which it is and mutes its notes. "
                : "Dorico is playing an edited or unknown copy of this chair's bridge map, so Fiddle mutes its "
                  "notes. ") +
                "Have the user quit Dorico, install the playback template, then reapply the Fiddle "
                "playback template in open projects."};
  if (beacon == "matched")
    return {"playing", "When playback started, Dorico sent this map's version ID and it matches."};
  if (verified)
    return {"playing", "The user confirmed from a Dorico export that Dorico has this map."};
  if (beacon == "absent" && othersReported)
    return {"notInProjectTemplate",
            "When playback started, Dorico sent other chairs their map's version ID but nothing to this "
            "one, so this project's playback template doesn't include it. If Dorico hasn't been restarted "
            "since the template was installed, restart it; then reapply the Fiddle playback template in "
            "this project."};
  if (beacon == "absent")
    return {"checkOnPlayback",
            "Fiddle checks this map's version when Dorico starts playback; play in Dorico to check."};
  return {"unchecked",
          "This map sends no version ID, so Fiddle can't check it and mutes its notes. The user can export "
          "the map from Dorico and choose Verify assigned map under Bridge maps in Fiddle's Chairs window."};
}

bool bridgeNeedsAttention(const juce::String &state, juce::int64 notesNotPlayed) {
  return notesNotPlayed > 0 ||
         (state != "playing" && state != "playingProjectRevision" && state != "checkOnPlayback");
}

juce::String suggestedRole(const BrowsableInstrument &instrument) {
  const auto family = instrument.family.toLowerCase();
  const bool section = family == "strings" || family == "string" || family == "choir" ||
                       family == "vocal" || family == "vocals" || family == "voice";
  return section ? "section" : "solo";
}

std::vector<juce::String> kitChairNames(const std::vector<juce::String> &names) {
  std::map<juce::String, int> totals, seen;
  for (const auto &name : names) ++totals[name];
  std::vector<juce::String> result;
  for (const auto &name : names)
    result.push_back(totals[name] > 1 ? name + " " + juce::String(++seen[name]) : name);
  return result;
}

LibraryEdit mergeLibraryEdit(const std::optional<LibraryCatalogSnapshot> &existing,
                             const juce::var &params, const LibraryEditChecks &checks,
                             const std::function<std::string()> &newId) {
  if (!params.isObject()) return failure("Library parameters must be an object");
  LibraryEdit edit;
  const auto requestedId = params["libraryId"].toString().trim().toStdString();
  const bool exists = existing && existing->exists;
  if (!requestedId.empty() && !exists)
    return failure("Library not found: " + juce::String(requestedId) +
                   ". Omit libraryId to create a new library.");
  edit.created = !exists;
  if (exists)
    edit.target = *existing;
  else
    edit.target.id = newId();
  edit.target.exists = true;
  if (const auto name = stringField(params, "name")) {
    if (name->empty()) return failure("Library name cannot be empty");
    edit.target.name = *name;
  }
  if (edit.target.name.empty()) return failure("A new library needs a name");
  if (const auto vendor = stringField(params, "vendor")) edit.target.vendor = *vendor;
  if (const auto variant = stringField(params, "variant")) edit.target.variant = *variant;
  const auto defaultPlugin = intField(params, "pluginUid");
  if (params.hasProperty("pluginUid") && !defaultPlugin)
    return failure("pluginUid must be an integer");
  if (defaultPlugin && !checks.instrumentPluginAvailable(*defaultPlugin))
    return failure("The plug-in " + juce::String(*defaultPlugin) +
                   " is not an installed instrument plug-in");

  // Removals first, so a request can replace a patch in one step.
  std::set<std::string> removed;
  if (const auto *ids = params["removePatchIds"].getArray()) {
    for (const auto &value : *ids) {
      const auto id = value.toString().trim().toStdString();
      const auto found = std::find_if(edit.target.patches.begin(), edit.target.patches.end(),
                                      [&](const auto &patch) { return patch.id == id; });
      if (found == edit.target.patches.end())
        return failure("Patch to remove is not in this library: " + juce::String(id));
      if (const int uses = checks.patchUsageCount(id); uses > 0)
        return failure("Cannot remove patch '" + juce::String(found->name) + "': " +
                       juce::String(uses) + (uses == 1 ? " layer uses" : " layers use") +
                       " it. Remove those layers first.");
      removed.insert(id);
    }
  }
  edit.target.patches.erase(
      std::remove_if(edit.target.patches.begin(), edit.target.patches.end(),
                     [&](const auto &patch) { return removed.count(patch.id) != 0; }),
      edit.target.patches.end());

  const auto *entries = params["patches"].getArray();
  if (params.hasProperty("patches") && !entries) return failure("patches must be an array");
  if (entries && entries->size() > 256) return failure("At most 256 patches per request");
  std::set<std::string> touched;
  if (entries) {
    for (const auto &entry : *entries) {
      if (!entry.isObject()) return failure("Every patch must be an object");
      const auto id = stringField(entry, "id").value_or("");
      LibraryPatchRow *patch = nullptr;
      LibraryPatchRow added;
      if (!id.empty()) {
        const auto found = std::find_if(edit.target.patches.begin(), edit.target.patches.end(),
                                        [&](const auto &row) { return row.id == id; });
        if (found == edit.target.patches.end())
          return failure("Patch not found in this library: " + juce::String(id) +
                         ". Omit id to add a new patch.");
        if (!touched.insert(id).second)
          return failure("A patch is listed twice: " + juce::String(id));
        patch = &*found;
      } else {
        added.id = newId();
        added.libraryId = edit.target.id;
        added.setupComplete = true;
        patch = &added;
      }
      const auto label = [&] { return juce::String(patch->name.empty() ? "new patch" : patch->name); };

      if (const auto name = stringField(entry, "name")) {
        if (name->empty()) return failure("Patch names cannot be empty");
        patch->name = *name;
      }
      if (patch->name.empty()) return failure("Every new patch needs a name");
      if (const auto instrument = stringField(entry, "instrumentEntityId")) {
        if (!instrument->empty() && !checks.instrumentExists(*instrument))
          return failure("Unknown Dorico instrument ID for " + label() + ": " + juce::String(*instrument));
        patch->instrumentEntityId = *instrument;
      }
      if (const auto family = stringField(entry, "family")) patch->family = *family;
      if (const auto character = stringField(entry, "character")) {
        if (!validCharacter(*character))
          return failure("Patch character must be solo, section, ensemble, overlay, or empty");
        patch->character = *character;
      }

      bool needsCapture = false;
      auto plugin = intField(entry, "pluginUid");
      if (entry.hasProperty("pluginUid") && !plugin) return failure("pluginUid must be an integer");
      if (!plugin && id.empty()) plugin = defaultPlugin;
      if (!plugin && id.empty())
        return failure("Patch " + label() + " needs a pluginUid (or give the library one)");
      if (plugin && *plugin != patch->pluginUid) {
        if (!checks.instrumentPluginAvailable(*plugin))
          return failure("The plug-in " + juce::String(*plugin) + " for " + label() +
                         " is not an installed instrument plug-in");
        patch->pluginUid = *plugin;
        patch->pluginState.clear();
        needsCapture = true;
      }
      if (const auto preset = stringField(entry, "expectedPresetName")) {
        if (*preset != patch->expectedPresetName) needsCapture = true;
        patch->expectedPresetName = *preset;
      }

      for (const auto kind : {LibraryMapKind::expression, LibraryMapKind::percussion}) {
        const bool expression = kind == LibraryMapKind::expression;
        const auto mapId = stringField(entry, expression ? "expressionMapId" : "percussionMapId");
        const auto path = stringField(entry, expression ? "expressionMapPath" : "percussionMapPath");
        if (path && !path->empty() && (!mapId || mapId->empty()))
          return failure(juce::String(expression ? "expressionMapPath" : "percussionMapPath") +
                         " needs the map's ID as well");
        if (!mapId) continue;
        const juce::String file = path ? juce::String(*path) : juce::String();
        if (!mapId->empty()) {
          if (!checks.mapAvailable(kind, *mapId, file))
            return failure(juce::String(expression ? "Expression" : "Percussion") + " map " +
                           juce::String(*mapId) +
                           (file.isEmpty() ? " is not in Fiddle's maps; give its .doricolib path"
                                           : " was not found in " + file));
          if (file.isNotEmpty()) {
            const MapImport import{kind, *mapId, file};
            if (std::find(edit.imports.begin(), edit.imports.end(), import) == edit.imports.end())
              edit.imports.push_back(import);
          }
        }
        (expression ? patch->expressionMapId : patch->percussionMapId) = *mapId;
      }

      // Play as defaults must name instruments the patch's percussion map has.
      if (entry.hasProperty("percussionPlayAs")) {
        const auto playAs = entry["percussionPlayAs"];
        if (!playAs.isObject() || !playAs.getDynamicObject())
          return failure("percussionPlayAs must map chair instrument IDs to map instruments");
        patch->percussionPlayAs.clear();
        for (const auto &property : playAs.getDynamicObject()->getProperties())
          patch->percussionPlayAs[property.name.toString().toStdString()] =
              property.value.toString().toStdString();
      }
      if (patch->percussionMapId.empty()) {
        if (!patch->percussionPlayAs.empty() && entry.hasProperty("percussionPlayAs"))
          return failure("percussionPlayAs needs a percussion map on " + label());
        patch->percussionPlayAs.clear();
      } else if (!patch->percussionPlayAs.empty()) {
        juce::String file;
        for (const auto &import : edit.imports)
          if (import.kind == LibraryMapKind::percussion && import.entityId == patch->percussionMapId)
            file = import.path;
        const auto listed = checks.percussionMapInstruments(patch->percussionMapId, file);
        if (!listed) return failure("Percussion map " + juce::String(patch->percussionMapId) + " is unavailable");
        for (const auto &[instrument, mapInstrument] : patch->percussionPlayAs) {
          if (std::find(listed->begin(), listed->end(), mapInstrument) == listed->end())
            return failure("Percussion map " + juce::String(patch->percussionMapId) +
                           " has no instrument " + juce::String(mapInstrument) + " (Play as for " +
                           juce::String(instrument) + ")");
        }
      }

      if (needsCapture && !patch->expectedPresetName.empty()) patch->setupComplete = false;
      if (id.empty()) {
        edit.addedPatchIds.push_back(added.id);
        edit.target.patches.push_back(std::move(added));
      }
    }
  }
  for (std::size_t position = 0; position < edit.target.patches.size(); ++position) {
    edit.target.patches[position].position = static_cast<int>(position);
    edit.target.patches[position].libraryId = edit.target.id;
  }
  if (edit.created && edit.target.patches.empty())
    return failure("A new library needs at least one patch");
  edit.ok = true;
  return edit;
}

} // namespace fiddle::agent
