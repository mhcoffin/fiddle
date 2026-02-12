#pragma once

#include "DoricoConfigGenerator.h"
#include "DoricoInstruments.h"
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace fiddle {

/**
 * UI Component that lets the user select which instruments to expose
 * to Dorico, and generates/installs the necessary configuration files.
 *
 * Instruments are displayed grouped by category (Strings, Woodwinds, etc.)
 * with checkboxes for each. A "Generate & Install" button triggers the
 * file generation.
 */
class DoricoSetupComponent : public juce::Component,
                             private juce::Button::Listener {
public:
  DoricoSetupComponent();
  ~DoricoSetupComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  void buttonClicked(juce::Button *button) override;

  void buildInstrumentList();
  void onGenerateClicked();
  void selectAll(bool shouldBeSelected);

  // ── UI Elements ──────────────────────────────────────────
  juce::Viewport viewport;
  std::unique_ptr<juce::Component> contentComponent;

  // One toggle button per instrument (indexed same as getDefaultInstruments())
  std::vector<std::unique_ptr<juce::ToggleButton>> instrumentToggles;

  // Category headers
  std::vector<std::unique_ptr<juce::Label>> categoryLabels;

  juce::TextButton selectAllButton{"Select All"};
  juce::TextButton deselectAllButton{"Deselect All"};
  juce::TextButton generateButton{"Generate & Install Configuration"};

  juce::Label statusLabel;
  juce::Label targetPathLabel;

  DoricoConfigGenerator generator;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DoricoSetupComponent)
};

} // namespace fiddle
