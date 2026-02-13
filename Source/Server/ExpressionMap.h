#pragma once

#include <algorithm>
#include <iostream>
#include <juce_core/juce_core.h>
#include <map>
#include <set>
#include <vector>

namespace fiddle {

/**
 * Loads and manages expression map data from a .doricolib XML file.
 *
 * Parses:
 * - Base switches (playingTechniqueCombinations) → "Articulation" dimension
 * - Add-on switches (techniqueAddOns) → one dimension per mutual exclusion
 * group
 * - Mutual exclusion groups → group names become dimension/attribute names
 *
 * The mapping chain: CC# + CC value → technique name → attribute value
 */
class ExpressionMap {
public:
  struct Dimension {
    juce::String name;
    int ccNumber = -1;
    std::vector<int> defaultValues;
    // Maps CC value to technique name (e.g., 0 -> "Natural")
    std::map<int, juce::String> techniques;
  };

  ExpressionMap() = default;

  bool loadFromDoricolib(const juce::File &file) {
    if (!file.existsAsFile())
      return false;

    auto xml = juce::XmlDocument::parse(file);
    if (!xml)
      return false;

    dimensions.clear();
    ccToDimensionIdx.clear();

    // Find the ExpressionMapDefinition named "Fiddle"
    auto *exprMapDefs = xml->getChildByName("expressionMapDefinitions");
    if (!exprMapDefs)
      return false;

    auto *entities = exprMapDefs->getChildByName("entities");
    if (!entities)
      return false;

    juce::XmlElement *fiddleMap = nullptr;
    for (auto *child : entities->getChildIterator()) {
      if (child->getStringAttribute("", "") == "ExpressionMapDefinition" ||
          child->getTagName() == "ExpressionMapDefinition") {
        auto *nameEl = child->getChildByName("name");
        if (nameEl && nameEl->getAllSubText() == "Fiddle") {
          fiddleMap = child;
          break;
        }
      }
    }
    if (!fiddleMap)
      return false;

    // ---------------------------------------------------------------
    // Step 1: Parse techniqueAddOns → build techniqueID → {cc, value, offValue}
    // ---------------------------------------------------------------
    struct AddOnInfo {
      juce::String techniqueID;
      int ccNumber = -1;
      int ccValue = -1;
      int offValue = -1; // from switchOffActions
    };
    std::vector<AddOnInfo> addOns;

    auto *addOnsEl = fiddleMap->getChildByName("techniqueAddOns");
    if (addOnsEl) {
      for (auto *addOn : addOnsEl->getChildIterator()) {
        AddOnInfo info;

        // Get techniqueIDs
        auto *techEl = addOn->getChildByName("techniqueIDs");
        if (techEl)
          info.techniqueID = techEl->getAllSubText().trim();

        // Get switchOnActions → CC number and value
        auto *onActions = addOn->getChildByName("switchOnActions");
        if (onActions) {
          for (auto *action : onActions->getChildIterator()) {
            auto *typeEl = action->getChildByName("type");
            if (typeEl && typeEl->getAllSubText().trim() == "kControlChange") {
              auto *p1 = action->getChildByName("param1");
              auto *p2 = action->getChildByName("param2");
              if (p1)
                info.ccNumber = p1->getAllSubText().trim().getIntValue();
              if (p2)
                info.ccValue = p2->getAllSubText().trim().getIntValue();
              break; // Only first CC action matters
            }
          }
        }

        // Get switchOffActions → default value for the CC
        auto *offActions = addOn->getChildByName("switchOffActions");
        if (offActions) {
          for (auto *action : offActions->getChildIterator()) {
            auto *typeEl = action->getChildByName("type");
            if (typeEl && typeEl->getAllSubText().trim() == "kControlChange") {
              auto *p2 = action->getChildByName("param2");
              if (p2)
                info.offValue = p2->getAllSubText().trim().getIntValue();
              break;
            }
          }
        }

        if (info.techniqueID.isNotEmpty() && info.ccNumber >= 0)
          addOns.push_back(info);
      }
    }

    // ---------------------------------------------------------------
    // Step 2: Parse mutualExclusionGroups → group name + member techniques
    // ---------------------------------------------------------------
    struct MEGInfo {
      juce::String name;
      std::vector<juce::String> techniqueIDs;
    };
    std::vector<MEGInfo> megs;

    auto *megsEl = fiddleMap->getChildByName("mutualExclusionGroups");
    if (megsEl) {
      for (auto *meg : megsEl->getChildIterator()) {
        MEGInfo info;
        auto *nameEl = meg->getChildByName("name");
        if (nameEl)
          info.name = nameEl->getAllSubText().trim();

        auto *techEl = meg->getChildByName("techniqueIDs");
        if (techEl) {
          juce::StringArray ids;
          ids.addTokens(techEl->getAllSubText(), ",", "");
          for (auto &id : ids)
            info.techniqueIDs.push_back(id.trim());
        }

        if (info.name.isNotEmpty())
          megs.push_back(info);
      }
    }

    // ---------------------------------------------------------------
    // Step 3: Build dimensions from MEGs
    // For each MEG, find the add-ons that belong to it, determine the
    // CC number, and build the technique → value mapping.
    // ---------------------------------------------------------------
    for (auto &meg : megs) {
      Dimension dim;
      dim.name = meg.name;

      // Find all add-ons that belong to this MEG (excluding pt.natural)
      std::set<int> ccNumbers;
      for (auto &techID : meg.techniqueIDs) {
        if (techID == "pt.natural")
          continue;

        for (auto &addOn : addOns) {
          if (addOn.techniqueID == techID) {
            ccNumbers.insert(addOn.ccNumber);

            // Build human-readable name from techniqueID
            juce::String displayName = humanizeTechniqueID(techID);
            dim.techniques[addOn.ccValue] = displayName;

            // Derive default value from switchOffActions
            if (addOn.offValue >= 0) {
              if (std::find(dim.defaultValues.begin(), dim.defaultValues.end(),
                            addOn.offValue) == dim.defaultValues.end()) {
                dim.defaultValues.push_back(addOn.offValue);
              }
            }
          }
        }
      }

      // Validate: all add-ons in this MEG should use the same CC
      if (ccNumbers.size() > 1) {
        std::cerr << "[ExpressionMap] WARNING: MEG '" << dim.name
                  << "' uses multiple CCs:";
        for (int cc : ccNumbers)
          std::cerr << " " << cc;
        std::cerr << std::endl;
      }

      if (!ccNumbers.empty()) {
        dim.ccNumber = *ccNumbers.begin();

        // Add "Natural" as the default technique at the default CC value
        for (int defVal : dim.defaultValues) {
          if (dim.techniques.find(defVal) == dim.techniques.end()) {
            dim.techniques[defVal] = "Natural";
          }
        }

        ccToDimensionIdx[dim.ccNumber] = (int)dimensions.size();
        dimensions.push_back(dim);

        std::cerr << "[ExpressionMap] MEG '" << dim.name << "' -> CC"
                  << dim.ccNumber << " (" << dim.techniques.size()
                  << " techniques, defaults:";
        for (int d : dim.defaultValues)
          std::cerr << " " << d;
        std::cerr << ")" << std::endl;
      }
    }

    // ---------------------------------------------------------------
    // Step 4: Build "Articulation" dimension from base switches
    // (playingTechniqueCombinations → CC102)
    // ---------------------------------------------------------------
    auto *combosEl = fiddleMap->getChildByName("playingTechniqueCombinations");
    if (combosEl) {
      Dimension articulationDim;
      articulationDim.name = "Articulation";

      for (auto *combo : combosEl->getChildIterator()) {
        auto *techEl = combo->getChildByName("techniqueIDs");
        if (!techEl)
          continue;

        juce::String techID = techEl->getAllSubText().trim();

        // Find the CC102 action
        auto *onActions = combo->getChildByName("switchOnActions");
        if (onActions) {
          for (auto *action : onActions->getChildIterator()) {
            auto *typeEl = action->getChildByName("type");
            if (typeEl && typeEl->getAllSubText().trim() == "kControlChange") {
              auto *p1 = action->getChildByName("param1");
              auto *p2 = action->getChildByName("param2");
              if (p1 && p1->getAllSubText().trim().getIntValue() == 102 && p2) {
                int ccVal = p2->getAllSubText().trim().getIntValue();
                juce::String displayName = humanizeTechniqueID(techID);
                articulationDim.techniques[ccVal] = displayName;
                articulationDim.ccNumber = 102;

                // pt.natural is the default
                if (techID == "pt.natural") {
                  articulationDim.defaultValues.push_back(ccVal);
                }
              }
              break;
            }
          }
        }
      }

      if (articulationDim.ccNumber >= 0 &&
          !articulationDim.techniques.empty()) {
        ccToDimensionIdx[articulationDim.ccNumber] = (int)dimensions.size();
        dimensions.push_back(articulationDim);

        std::cerr << "[ExpressionMap] Articulation -> CC"
                  << articulationDim.ccNumber << " ("
                  << articulationDim.techniques.size() << " techniques)"
                  << std::endl;
      }
    }

    std::cerr << "[ExpressionMap] Loaded " << dimensions.size()
              << " dimensions from " << file.getFileName() << std::endl;
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

  /// Convert "pt.staccato" → "Staccato", "pt.user.glissando" → "Glissando",
  /// "pt.staccato+pt.tenuto" → "Staccato+Tenuto", etc.
  static juce::String humanizeTechniqueID(const juce::String &techID) {
    // Handle combined techniques like "pt.staccato+pt.tenuto"
    if (techID.contains("+")) {
      juce::StringArray parts;
      parts.addTokens(techID, "+", "");
      juce::StringArray humanized;
      for (auto &part : parts)
        humanized.add(humanizeSingle(part.trim()));
      return humanized.joinIntoString("+");
    }
    return humanizeSingle(techID);
  }

  static juce::String humanizeSingle(const juce::String &techID) {
    juce::String name = techID;

    // Strip prefixes
    if (name.startsWith("pt.user."))
      name = name.substring(8);
    else if (name.startsWith("pt."))
      name = name.substring(3);

    if (name.isEmpty())
      return techID; // fallback

    // Capitalize first letter
    return name.substring(0, 1).toUpperCase() + name.substring(1);
  }
};

} // namespace fiddle
