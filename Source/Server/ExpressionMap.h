#pragma once

#include <algorithm>
#include <iostream>
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace fiddle {

/**
 * Loads and manages expression map data from a CSV file.
 * Format: Mutex Group, CC#, [Ignored], CC Value
 */
class ExpressionMap {
public:
  struct Dimension {
    juce::String name;
    int ccNumber = -1;
    std::vector<int> defaultValues;
    // Maps CC value to technique name (e.g., 0 -> "pt.natural")
    std::map<int, juce::String> techniques;
  };

  ExpressionMap() = default;

  bool loadFromCsv(const juce::File &file) {
    if (!file.existsAsFile())
      return false;

    juce::StringArray lines;
    file.readLines(lines);

    dimensions.clear();
    ccToDimensionIdx.clear();

    Dimension *currentDim = nullptr;

    // Skip header
    for (int i = 1; i < lines.size(); ++i) {
      juce::StringArray tokens;
      tokens.addTokens(lines[i], ",", "\"");

      if (tokens.size() < 4)
        continue;

      juce::String groupName = tokens[0].trim();
      juce::String ccStr = tokens[1].trim();
      juce::String techName = tokens[2].trim();
      juce::String valueStr = tokens[3].trim();
      juce::String defaultStr = (tokens.size() > 4) ? tokens[4].trim() : "";

      if (groupName.isNotEmpty()) {
        dimensions.emplace_back();
        currentDim = &dimensions.back();
        currentDim->name = groupName;
        if (ccStr.isNotEmpty()) {
          currentDim->ccNumber = ccStr.getIntValue();
          ccToDimensionIdx[currentDim->ccNumber] = (int)dimensions.size() - 1;
        }
      }

      if (currentDim != nullptr) {
        if (valueStr.isNotEmpty()) {
          int value = valueStr.getIntValue();
          currentDim->techniques[value] = techName;

          if (defaultStr.equalsIgnoreCase("TRUE")) {
            if (std::find(currentDim->defaultValues.begin(),
                          currentDim->defaultValues.end(),
                          value) == currentDim->defaultValues.end()) {
              currentDim->defaultValues.push_back(value);
              std::cerr << "[ExpressionMap] " << currentDim->name
                        << " Default added: " << value << " (" << techName
                        << ")" << std::endl;
            }
          }
        }
      }
    }

    return !dimensions.empty();
  }

  const std::vector<Dimension> &getDimensions() const { return dimensions; }

  const Dimension *getDimensionForCC(int cc) const {
    auto it = ccToDimensionIdx.find(cc);
    if (it != ccToDimensionIdx.end()) {
      return &dimensions[it->second];
    }
    return nullptr;
  }

private:
  std::vector<Dimension> dimensions;
  std::map<int, int> ccToDimensionIdx;
};

} // namespace fiddle
