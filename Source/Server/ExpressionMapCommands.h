#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace fiddle {

/// Operations exposed by the expression-map command boundary.
class ExpressionMapCommands {
public:
  virtual ~ExpressionMapCommands() = default;

  [[nodiscard]] virtual juce::var catalog() const = 0;
  /// A map's contents for the viewer: from libraryId's maps when given,
  /// otherwise from Dorico's library folders.
  [[nodiscard]] virtual juce::var details(const juce::String &entityId,
                                          const juce::String &libraryId = {}) = 0;
  virtual bool assign(const juce::String &stripId,
                      const juce::String &entityId) = 0;
  virtual bool clear(const juce::String &stripId) = 0;
  virtual bool assignGroup(const std::vector<juce::String> &stripIds,
                           const juce::String &entityId) = 0;
  virtual bool importFile(const juce::String &stripId,
                          const juce::File &file) = 0;
};

} // namespace fiddle
