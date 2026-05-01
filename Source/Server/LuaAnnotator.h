#pragma once

#include "Annotator.h"
#include "LuaPlugin.h"
#include <memory>

namespace fiddle {

/**
 * Annotator subclass that delegates to a LuaPlugin instance.
 *
 * This is what gets placed in the AnnotatorChain per-strip,
 * one LuaAnnotator per installed LuaPlugin.
 */
class LuaAnnotator : public Annotator {
public:
  explicit LuaAnnotator(std::shared_ptr<LuaPlugin> plugin)
      : plugin_(std::move(plugin)) {}

  void onNoteStart(fiddle::Note &note,
                   const AnnotatorContext &ctx) override {
    if (!plugin_ || !plugin_->isLoaded() || !plugin_->isEnabled())
      return;

    LuaContext luaCtx;
    luaCtx.stripChannel  = ctx.stripChannel;
    luaCtx.sampleRate    = ctx.sampleRate;
    luaCtx.currentTimeMs = ctx.currentTimeMs;
    luaCtx.tonalContext  = ctx.tonalContext;
    // instrumentFamily and isSolo are set by MixerStrip before calling

    plugin_->onNoteStart(note, luaCtx);

    // Read back scheduled note-off if the plugin set one
    scheduledNoteOffMs_ = plugin_->lastScheduledNoteOffMs();
  }

  void onNoteEnd(fiddle::Note &note,
                 const AnnotatorContext &ctx) override {
    durationAdjustMs_ = 0.0; // reset each note
    if (!plugin_ || !plugin_->isLoaded() || !plugin_->isEnabled())
      return;

    LuaContext luaCtx;
    luaCtx.stripChannel  = ctx.stripChannel;
    luaCtx.sampleRate    = ctx.sampleRate;
    luaCtx.currentTimeMs = ctx.currentTimeMs;
    luaCtx.tonalContext  = ctx.tonalContext;

    plugin_->onNoteEnd(note, luaCtx);

    // Read back duration adjustment if the plugin set one
    durationAdjustMs_ = plugin_->lastDurationAdjustMs();
  }

  bool onCC(const fiddle::MidiEvent &event,
            const AnnotatorContext &ctx) override {
    if (!plugin_ || !plugin_->isLoaded() || !plugin_->isEnabled())
      return true; // passthrough when disabled

    if (!event.has_cc())
      return true;

    LuaContext luaCtx;
    luaCtx.stripChannel = ctx.stripChannel;
    luaCtx.sampleRate = ctx.sampleRate;
    luaCtx.currentTimeMs = ctx.currentTimeMs;

    return plugin_->onCC((int)event.cc().controller_number(),
                         (int)event.cc().controller_value(),
                         ctx.stripChannel, luaCtx);
  }

  std::string name() const override {
    if (plugin_)
      return plugin_->meta().name;
    return "Lua (unloaded)";
  }

  double durationAdjustMs() const override {
    return durationAdjustMs_;
  }

  double scheduledNoteOffMs() const override {
    return scheduledNoteOffMs_;
  }

  void resetState() override {
    if (plugin_)
      plugin_->reset();
  }

  /// Access the underlying plugin (for config UI).
  LuaPlugin *plugin() const { return plugin_.get(); }

private:
  std::shared_ptr<LuaPlugin> plugin_;
  double durationAdjustMs_ = 0.0;
  double scheduledNoteOffMs_ = -1.0;
};

} // namespace fiddle
