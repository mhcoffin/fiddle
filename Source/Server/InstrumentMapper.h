#pragma once

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace fiddle {

/**
 * InstrumentMapper: maps MIDI program numbers to human-readable instrument
 * names (from Dorico's presets.xml) and tracks per-channel assignments.
 *
 * Usage:
 *   InstrumentMapper mapper;                          // parses presets.xml
 *   auto name = mapper.handleProgramChange(0, 1);     // returns "Violin"
 *   auto name = mapper.getInstrumentName(0);           // returns "Violin"
 */
class InstrumentMapper {
public:
  InstrumentMapper() { loadPresetNames(); }

  /// Process a program change on a channel. Returns the instrument name.
  std::string handleProgramChange(int channel, int program) {
    std::string name = getPresetName(program);
    if (!name.empty())
      channelInstruments_[channel] = name;
    else
      channelInstruments_.erase(channel);
    return name;
  }

  /// Get the current instrument name for a channel (empty if unset).
  std::string getInstrumentName(int channel) const {
    auto it = channelInstruments_.find(channel);
    return it != channelInstruments_.end() ? it->second : "";
  }

  /// Look up preset name by program number (empty if unknown).
  std::string getPresetName(int program) const {
    auto it = presetNames_.find(program);
    return it != presetNames_.end() ? it->second : "";
  }

  /// Get all current channel → instrument assignments.
  const std::map<int, std::string> &getChannelInstruments() const {
    return channelInstruments_;
  }

  /// Clear all channel assignments (e.g. on transport reset).
  void clearChannelAssignments() { channelInstruments_.clear(); }

private:
  void loadPresetNames() {
    const char *home = std::getenv("HOME");
    if (!home)
      return;

    std::string path = std::string(home) +
                       "/Library/Application Support/Steinberg/Dorico 6/"
                       "PluginPresetLibraries/Fiddle/presets.xml";

    std::ifstream file(path);
    if (!file.is_open())
      return;

    std::stringstream buf;
    buf << file.rdbuf();
    std::string xml = buf.str();

    // Simple XML parsing: find each <Preset>...</Preset> block,
    // extract <Name> and <Program> values.
    size_t pos = 0;
    while (true) {
      size_t presetStart = xml.find("<Preset>", pos);
      if (presetStart == std::string::npos)
        break;
      size_t presetEnd = xml.find("</Preset>", presetStart);
      if (presetEnd == std::string::npos)
        break;

      std::string block = xml.substr(presetStart, presetEnd - presetStart);

      // Extract <Name>...</Name>
      std::string name;
      size_t nameStart = block.find("<Name>");
      size_t nameEnd = block.find("</Name>");
      if (nameStart != std::string::npos && nameEnd != std::string::npos) {
        nameStart += 6; // strlen("<Name>")
        name = block.substr(nameStart, nameEnd - nameStart);
      }

      // Extract <Program>...</Program>
      int prog = -1;
      size_t progStart = block.find("<Program>");
      size_t progEnd = block.find("</Program>");
      if (progStart != std::string::npos && progEnd != std::string::npos) {
        progStart += 9; // strlen("<Program>")
        prog = std::atoi(block.substr(progStart, progEnd - progStart).c_str());
      }

      if (prog >= 0 && !name.empty()) {
        // Strip "Fiddle_" prefix if present for cleaner display
        const std::string prefix = "Fiddle_";
        if (name.compare(0, prefix.size(), prefix) == 0)
          name = name.substr(prefix.size());
        presetNames_[prog] = name;
      }

      pos = presetEnd + 9;
    }
  }

  std::map<int, std::string> presetNames_;        // program → name
  std::map<int, std::string> channelInstruments_; // channel → name
};

} // namespace fiddle
