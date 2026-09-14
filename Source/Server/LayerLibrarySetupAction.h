#pragma once

#include "../AudioProcessingGate.h"
#include "LibraryRoutingRepository.h"
#include "MixerModel.h"
#include "UndoManager.h"

namespace fiddle {

// A saved setup is independent of the current catalog, including imported maps
// and unavailable/pending players. Mixing, routing, Lua and FX belong to the
// layer and are deliberately not replaced by a library refresh.
struct LayerLibrarySetup {
  LayerRow row;
  MixerStrip::InstrumentSnapshot instrument;
  std::shared_ptr<ExpressionMapData> expressionMap;
  juce::String expressionMapPath;
  juce::String expressionMapSourceXml;
  bool missingPatchReference = false;
};

class LayerLibrarySetupAction final : public UndoableAction {
public:
  // Called immediately after applying a setup and after async loading settles.
  // The owner must defer persistence until UndoManager has moved its identity.
  using Settled = std::function<void(const juce::String &)>;

  LayerLibrarySetupAction(LibraryRoutingRepository &repository, MixerModel &mixer,
                         std::vector<LayerLibrarySetup> targets,
                         juce::String description, Settled settled = {})
      : repository_(repository), mixer_(mixer), after_(std::move(targets)),
        description_(std::move(description)), settled_(std::move(settled)) {}

  void execute() override { swapTo(after_, before_); }
  void undo() override { swapTo(before_, after_); }
  bool succeeded() const override { return success_; }
  bool isNoOp() const override { return after_.empty(); }
  juce::String getDescription() const override { return description_; }

private:
  void swapTo(const std::vector<LayerLibrarySetup> &targets,
              std::vector<LayerLibrarySetup> &departing) {
    success_ = false;
    std::vector<LayerLibrarySetup> captured;
    std::vector<LayerRow> rows;
    // Preflight the entire set before capturing or modifying any live player.
    for (const auto &target : targets) {
      auto *strip = mixer_.getStrip(juce::String(target.row.id));
      const auto row = repository_.getLayer(target.row.id);
      if (!strip || !row || row->chairId != target.row.chairId ||
          row->patchId != target.row.patchId ||
          strip->chairId.toStdString() != row->chairId ||
          strip->patchId.toStdString() != row->patchId) return;
      rows.push_back(target.row);
    }
    for (const auto &target : targets) {
      auto &strip = *mixer_.getStrip(juce::String(target.row.id));
      LayerLibrarySetup current;
      current.row = *repository_.getLayer(target.row.id);
      current.instrument = strip.snapshotInstrument();
      const auto map = strip.snapshotExpressionMap();
      current.expressionMap = map.data;
      current.expressionMapPath = map.sourcePath;
      current.expressionMapSourceXml = map.sourceXml;
      current.missingPatchReference = strip.missingPatchReference;
      current.row.patchName = strip.layerName.toStdString();
      current.row.libraryName = strip.library.toStdString();
      current.row.pluginUid = current.instrument.description.uniqueId;
      const auto &state = current.instrument.state;
      current.row.pluginState.clear();
      if (!state.isEmpty()) {
        const auto *bytes = static_cast<const uint8_t *>(state.getData());
        current.row.pluginState.assign(bytes, bytes + state.getSize());
      }
      current.row.expressionMapId = current.expressionMap ? current.expressionMap->entityID : "";
      captured.push_back(std::move(current));
    }
    // A SQL failure rolls back every row and leaves live state/history intact.
    if (!repository_.updateLayerSetups(rows)) return;
    departing = std::move(captured); // Refresh snapshots on every undo/redo cycle.
    for (const auto &target : targets) apply(target);
    success_ = true;
  }

  void apply(const LayerLibrarySetup &target) {
    AudioProcessingGate::Control control;
    auto *strip = mixer_.getStrip(juce::String(target.row.id));
    strip->allNotesOff();
    strip->layerName = target.row.patchName;
    strip->library = target.row.libraryName;
    strip->missingPatchReference = target.missingPatchReference;
    strip->setExpressionMapAssignment(
        {target.expressionMap, target.expressionMapPath,
         target.expressionMapSourceXml});
    const auto &instrument = target.instrument;
    const auto uid = instrument.description.uniqueId;
    const auto settled = settled_;
    const auto finish = [strip, settled](bool) {
      if (settled) settled(strip->id);
    };
    if (uid != 0 && strip->pluginUid == uid && strip->hasPlugin() &&
        !instrument.state.isEmpty() &&
        strip->applyPluginState(instrument.state.getData(), (int)instrument.state.getSize())) {
      strip->setPluginBypassed(instrument.bypassed);
      finish(true);
      return;
    }
    strip->unloadPlugin(); // Cancels a superseded async load, including immediate Undo.
    strip->setPluginBypassed(instrument.bypassed);
    if (uid == 0) { finish(true); return; }
    strip->pluginUid = uid;
    strip->loadPlugin(instrument.description, mixer_.getFormatManager(), finish, instrument.state);
    // Preserve requested bypass while loading; callbacks never capture this action.
    strip->setPluginBypassed(instrument.bypassed);
    if (settled) settled(strip->id);
  }

  LibraryRoutingRepository &repository_;
  MixerModel &mixer_;
  std::vector<LayerLibrarySetup> before_, after_;
  juce::String description_;
  Settled settled_;
  bool success_ = false;
};

} // namespace fiddle
