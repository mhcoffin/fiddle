#pragma once

#include "MixerModel.h"
#include "PluginScanner.h"
#include <fstream>
#include <iostream>
#include <juce_core/juce_core.h>
#include <yaml-cpp/yaml.h>

namespace fiddle {

class FiddleConfig {
public:
  /// Root app data directory: ~/Library/Application Support/Fiddle/
  static juce::File getAppDataDir() {
    auto dir =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Fiddle");
    if (!dir.exists())
      dir.createDirectory();
    return dir;
  }

  /// Config files directory: ~/Library/Application Support/Fiddle/configs/
  static juce::File getConfigDir() {
    auto dir = getAppDataDir().getChildFile("configs");
    if (!dir.exists())
      dir.createDirectory();
    return dir;
  }

  /// Legacy single-config path (for migration)
  static juce::File getConfigPath() {
    return getAppDataDir().getChildFile("config.yaml");
  }

  /// Recent configs tracking file
  static juce::File getRecentConfigsFile() {
    return getAppDataDir().getChildFile("recent_configs.json");
  }

  /// Load list of recently-used config files (most recent first)
  static juce::StringArray loadRecentConfigs() {
    juce::StringArray paths;
    auto file = getRecentConfigsFile();
    if (!file.existsAsFile())
      return paths;

    auto parsed = juce::JSON::parse(file);
    if (auto *arr = parsed.getArray()) {
      for (auto &v : *arr)
        paths.add(v.toString());
    }
    return paths;
  }

  /// Add/move a config path to the front of the recents list (max 10)
  static void saveRecentConfig(const juce::File &configFile) {
    auto paths = loadRecentConfigs();
    juce::String fullPath = configFile.getFullPathName();

    // Remove if already in list, then prepend
    paths.removeString(fullPath);
    paths.insert(0, fullPath);

    // Cap at 10
    while (paths.size() > 10)
      paths.remove(paths.size() - 1);

    // Write JSON array
    juce::var arr;
    for (auto &p : paths)
      arr.append(p);

    auto json = juce::JSON::toString(arr);
    getRecentConfigsFile().replaceWithText(json);
  }

  /// Create a new empty config file in the configs directory
  static juce::File createNewConfig(const juce::String &name) {
    auto file = getConfigDir().getChildFile(name + ".yaml");
    if (!file.existsAsFile()) {
      // Write a minimal YAML skeleton
      file.replaceWithText("# Fiddle Configuration: " + name +
                           "\n"
                           "mixer_strips: []\n");
    }
    return file;
  }

  /// Migrate legacy ~/Library/Application Support/Fiddle/config.yaml -> 
  /// configs/Default.yaml if needed.
  /// Returns the migrated file, or juce::File() if nothing to migrate.
  static juce::File migrateLegacyConfig() {
    auto legacy = getConfigPath();
    if (!legacy.existsAsFile())
      return {};

    auto dest = getConfigDir().getChildFile("Default.yaml");
    if (!dest.existsAsFile()) {
      legacy.copyFileTo(dest);
      saveRecentConfig(dest);
      std::cerr
          << "[FiddleConfig] Migrated legacy config.yaml -> configs/Default.yaml"
          << std::endl;
    }
    return dest;
  }

  static void save(const PluginScanner &scanner, const MixerModel &mixer,
                   const juce::File &targetFile) {
    YAML::Node root;

    // Save Plugin Scanner Cache
    if (auto xml = scanner.getKnownPluginList().createXml()) {
      root["plugin_cache"] = xml->createDocument(juce::String()).toStdString();
    }

    // Save Mixer Strips
    YAML::Node stripsNode;
    juce::String json = mixer.toJson();
    juce::var parsed = juce::JSON::parse(json);
    if (auto *arr = parsed.getArray()) {
      for (auto &v : *arr) {
        if (auto *obj = v.getDynamicObject()) {
          YAML::Node strip;
          strip["id"] = obj->getProperty("id").toString().toStdString();
          strip["name"] = obj->getProperty("name").toString().toStdString();
          strip["inputPort"] = static_cast<int>(obj->getProperty("inputPort"));
          strip["inputChannel"] =
              static_cast<int>(obj->getProperty("inputChannel"));
          strip["pluginUid"] = static_cast<int>(obj->getProperty("pluginUid"));

          // Store raw plugin state if loaded
          if (auto *mixerStrip = const_cast<MixerModel &>(mixer).getStrip(
                  strip["id"].as<std::string>().c_str())) {
            if (mixerStrip->pluginInstance) {
              juce::MemoryBlock block;
              mixerStrip->pluginInstance->getStateInformation(block);
              if (block.getSize() > 0) {
                strip["state"] = block.toBase64Encoding().toStdString();
              }
            }
          }
          stripsNode.push_back(strip);
        }
      }
    }
    root["mixer_strips"] = stripsNode;

    juce::String fullPath = targetFile.getFullPathName();
    std::cerr << "[FiddleConfig] Writing YAML to: " << fullPath << std::endl;

    std::ofstream fout(fullPath.toStdString());
    if (!fout.is_open()) {
      std::cerr << "[FiddleConfig] ERRROR: Could not open file for writing!"
                << std::endl;
    }
    fout << root;
  }

  static void save(const PluginScanner &scanner, const MixerModel &mixer) {
    save(scanner, mixer, getConfigPath());
  }

  static std::vector<juce::String> load(PluginScanner &scanner,
                                        MixerModel &mixer,
                                        const juce::File &sourceFile) {
    std::vector<juce::String> logs;
    auto path = sourceFile;
    if (!path.existsAsFile()) {
      logs.push_back("No config file found at " + path.getFullPathName());
      return logs;
    }

    try {
      YAML::Node root = YAML::LoadFile(path.getFullPathName().toStdString());
      logs.push_back("Loaded config YAML from " + path.getFullPathName());

      // Load Scanner Cache
      if (root["plugin_cache"].IsDefined() && !root["plugin_cache"].IsNull()) {
        juce::String xmlStr = root["plugin_cache"].as<std::string>();
        if (auto xml = juce::XmlDocument::parse(xmlStr)) {
          if (xml->hasTagName("KNOWNPLUGINS")) {
            scanner.getKnownPluginListMutable().recreateFromXml(*xml);
            logs.push_back("Restored known plugins cache with " +
                           juce::String(scanner.getPluginCount()) +
                           " plugins.");
          } else {
            logs.push_back("XML root tag was not KNOWNPLUGINS.");
          }
        } else {
          logs.push_back("Failed to parse plugin_cache as XML.");
        }
      }

      // Load Mixer Strips
      if (root["mixer_strips"].IsDefined() && !root["mixer_strips"].IsNull()) {
        if (root["mixer_strips"].IsSequence()) {
          logs.push_back("Found mixer_strips sequence with " +
                         juce::String((int)root["mixer_strips"].size()) +
                         " items.");
          for (const auto &node : root["mixer_strips"]) {
            juce::String newId = mixer.addStrip();
            if (auto *strip = mixer.getStrip(newId)) {
              strip->name = node["name"].as<std::string>();
              strip->inputPort = node["inputPort"].as<int>();
              strip->inputChannel = node["inputChannel"].as<int>();

              logs.push_back("Restored strip: " + strip->name +
                             " (Port: " + juce::String(strip->inputPort) +
                             ", Ch: " + juce::String(strip->inputChannel) +
                             ")");

              int pUid = node["pluginUid"].as<int>();
              if (pUid != 0) {
                juce::PluginDescription desc;
                bool found = false;
                for (const auto &d : scanner.getKnownPluginList().getTypes()) {
                  if (d.uniqueId == pUid) {
                    desc = d;
                    found = true;
                    break;
                  }
                }
                if (found) {
                  juce::String stateBase64 =
                      node["state"] ? node["state"].as<std::string>() : "";
                  // Capture format manager safely
                  strip->loadPlugin(
                      desc, mixer.getFormatManager(),
                      [strip, stateBase64](bool success) {
                        if (success && stateBase64.isNotEmpty() &&
                            strip->pluginInstance) {
                          juce::MemoryBlock block;
                          block.fromBase64Encoding(stateBase64);
                          strip->pluginInstance->setStateInformation(
                              block.getData(), (int)block.getSize());
                        }
                      });
                } else {
                  logs.push_back("WARNING: Plugin UID " + juce::String(pUid) +
                                 " not found in scanner cache for strip " +
                                 strip->name);
                }
              }
            }
          }
        } else {
          logs.push_back(
              "mixer_strips node exists but is not a sequence. Type: " +
              juce::String(root["mixer_strips"].Type()));
        }
      } else {
        juce::String availKeys = "Available keys: ";
        for (YAML::const_iterator it = root.begin(); it != root.end(); ++it) {
          availKeys += juce::String(it->first.as<std::string>()) + ", ";
        }
        logs.push_back("No mixer_strips sequence found in YAML. " + availKeys);
      }
    } catch (const YAML::Exception &e) {
      logs.push_back(juce::String("YAML parsing error: ") + e.what());
    }
    return logs;
  }

  static std::vector<juce::String> load(PluginScanner &scanner,
                                        MixerModel &mixer) {
    return load(scanner, mixer, getConfigPath());
  }
};

} // namespace fiddle
