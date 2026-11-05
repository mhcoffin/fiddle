#pragma once

#include "LibraryRoutingRepository.h"
#include "VersionTypes.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fiddle {

// Project versions saved before chairs existed hold free-standing mixer
// strips: a plug-in on a MIDI port and channel, with a free-text library name
// and no Dorico instrument. Restoring one converts it to the chair model
// (docs/library-map-import.md, "Every layer has a library"):
//
// - Strips with the same library name share a generated library, named
//   "<name> (restored)" or "Unfiled (restored)". Its ID is derived from the
//   name, so restoring old versions again reuses it.
// - Each distinct plug-in setup becomes a patch in that library, with the
//   strip's expression and percussion maps imported into the library.
// - Each MIDI port and channel becomes a chair at the same input. Where the
//   current project already has a chair on that input, the new chair copies
//   its instrument, so playback through Dorico's current template still
//   reaches it. Otherwise the instrument is a stand-in for the strip's family.
// - Each strip that holds anything becomes a layer on its chair. Strips with
//   no plug-in, maps, scripts or effects (the empty strips old templates made
//   for every channel) leave just the chair.

struct LegacyStripConversion {
  /// Generated libraries, with only the patches and maps this version needs.
  /// Merge each with any stored copy (mergeRestoredLibrary) before writing.
  std::vector<LibraryCatalogSnapshot> libraries;
  std::vector<ChairRow> chairs;
  std::vector<LayerRow> layers;
  /// Layer ID -> the strip blob it was made from.
  std::map<std::string, versioning::Hash> layerHashes;
};

std::string restoredLibraryId(const std::string &label);
std::string restoredLibraryName(const std::string &label);
/// The Dorico instrument a new chair gets when nothing better is known.
std::string standInInstrument(const std::string &family);

/// strips is the version's strip list in order. liveChairs is the current
/// project's chairs. newId makes layer IDs.
LegacyStripConversion convertLegacyStrips(
    const std::vector<std::pair<versioning::Hash, versioning::StripBlob>> &strips,
    const std::vector<ChairRow> &liveChairs,
    const std::function<std::string()> &newId);

/// generated plus whatever the stored library already has. Stored patches and
/// maps win, so restoring never rewrites a patch the user has since edited.
LibraryCatalogSnapshot mergeRestoredLibrary(const std::optional<LibraryCatalogSnapshot> &stored,
                                            const LibraryCatalogSnapshot &generated);

} // namespace fiddle
