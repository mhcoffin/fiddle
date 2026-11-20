#include "ProjectEnvironment.h"

#include <cmath>

namespace fiddle {

juce::var ProjectEnvironment::toJson() const {
  auto *o = new juce::DynamicObject();
  o->setProperty("schema", 1);
  o->setProperty("appVersion", appVersion);
  o->setProperty("sampleRate", sampleRate);
  juce::Array<juce::var> pluginList;
  for (const auto &[uid, plugin] : plugins) {
    auto *p = new juce::DynamicObject();
    p->setProperty("uid", uid);
    p->setProperty("name", plugin.name);
    p->setProperty("manufacturer", plugin.manufacturer);
    p->setProperty("version", plugin.version);
    p->setProperty("fileTime", plugin.fileTime);
    p->setProperty("path", plugin.path);
    p->setProperty("instrument", plugin.instrument);
    pluginList.add(juce::var(p));
  }
  o->setProperty("plugins", pluginList);
  juce::Array<juce::var> scriptList;
  for (const auto &[name, script] : scripts) {
    auto *s = new juce::DynamicObject();
    s->setProperty("name", name);
    s->setProperty("filePath", script.filePath);
    s->setProperty("fingerprint", script.fingerprint);
    scriptList.add(juce::var(s));
  }
  o->setProperty("scripts", scriptList);
  return juce::var(o);
}

std::optional<ProjectEnvironment> ProjectEnvironment::fromJson(const juce::var &json) {
  if (!json.isObject() || int(json["schema"]) != 1)
    return std::nullopt;
  ProjectEnvironment environment;
  environment.appVersion = json["appVersion"].toString();
  environment.sampleRate = double(json["sampleRate"]);
  if (const auto *list = json["plugins"].getArray())
    for (const auto &p : *list)
      environment.plugins[int(p["uid"])] = {p["name"].toString(), p["manufacturer"].toString(),
                                            p["version"].toString(), juce::int64(p["fileTime"]),
                                            p["path"].toString(), bool(p["instrument"])};
  if (const auto *list = json["scripts"].getArray())
    for (const auto &s : *list)
      environment.scripts[s["name"].toString()] = {s["filePath"].toString(),
                                                   s["fingerprint"].toString()};
  return environment;
}

juce::String ProjectEnvironment::fingerprintOf(const juce::String &text) {
  return juce::String::toHexString(text.hashCode64());
}

namespace {
juce::String rateText(double rate) {
  const auto kHz = rate / 1000.0;
  return (std::abs(kHz - std::round(kHz)) < 0.01 ? juce::String(juce::roundToInt(kHz))
                                                  : juce::String(kHz, 1)) + " kHz";
}
} // namespace

std::vector<juce::String> environmentChanges(const ProjectEnvironment &saved,
                                             const ProjectEnvironment &now) {
  std::vector<juce::String> changes;
  if (saved.appVersion.isNotEmpty() && now.appVersion.isNotEmpty() &&
      saved.appVersion != now.appVersion)
    changes.push_back("Fiddle has been updated (" + saved.appVersion + " to " + now.appVersion +
                      "). A new release can change how Fiddle plays the same setup.");
  if (saved.sampleRate > 0.0 && now.sampleRate > 0.0 &&
      std::abs(saved.sampleRate - now.sampleRate) > 0.5)
    changes.push_back("The audio sample rate was " + rateText(saved.sampleRate) +
                      " when this project was saved and is " + rateText(now.sampleRate) + " now.");
  for (const auto &[uid, before] : saved.plugins) {
    const auto current = now.plugins.find(uid);
    if (current == now.plugins.end()) {
      // A missing instrument silences its layer, which the banner reports.
      if (!before.instrument)
        changes.push_back("The effect plug-in " + before.name +
                          " is no longer installed, so its inserts are skipped.");
      continue;
    }
    const auto &after = current->second;
    const auto name = before.name.isNotEmpty() ? before.name : after.name;
    if (before.version.isNotEmpty() && after.version.isNotEmpty() && before.version != after.version)
      changes.push_back(name + " has been updated (" + before.version + " to " + after.version + ").");
    else if (before.fileTime != 0 && after.fileTime != 0 && before.fileTime != after.fileTime)
      changes.push_back(name + " has been reinstalled or changed since this project was saved.");
  }
  for (const auto &[name, before] : saved.scripts) {
    const auto current = now.scripts.find(name);
    if (current == now.scripts.end() || current->second.fingerprint.isEmpty())
      continue; // missing: reported from the mixer
    if (before.fingerprint.isNotEmpty() && before.fingerprint != current->second.fingerprint)
      changes.push_back("The Fiddle script \"" + name + "\" has been edited since this project was saved.");
  }
  return changes;
}

} // namespace fiddle
