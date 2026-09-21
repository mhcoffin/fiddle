#pragma once
#include <cctype>
#include <string>

namespace fiddle {
// Legacy display-to-identity conversion shared by playback and its audit.
// A future versioned bridge must preserve canonical IDs instead of guessing.
inline std::string techniqueIdFromDisplay(const std::string &display) {
  if (display.empty()) return {};
  if (display.size() > 3 && display.compare(0, 3, "pt.") == 0) return display;
  std::string id = "pt.";
  for (const char c : display) {
    if (c == ' ' || c == '-' || c == '_') continue;
    id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return id;
}
} // namespace fiddle
