#include "DoricoSetupComponent.h"
#include <iostream>

namespace fiddle {

// ── Layout Constants ───────────────────────────────────────────────────────
static constexpr int kRowHeight = 24;
static constexpr int kCategoryHeaderHeight = 32;
static constexpr int kSectionGap = 12;
static constexpr int kLeftMargin = 20;
static constexpr int kCheckboxIndent = 40;
static constexpr int kButtonBarHeight = 80;

// ── Constructor / Destructor ───────────────────────────────────────────────

DoricoSetupComponent::DoricoSetupComponent() {
  contentComponent = std::make_unique<juce::Component>();
  viewport.setViewedComponent(contentComponent.get(), false);
  viewport.setScrollBarsShown(true, false);
  addAndMakeVisible(viewport);

  addAndMakeVisible(selectAllButton);
  addAndMakeVisible(deselectAllButton);
  addAndMakeVisible(generateButton);
  addAndMakeVisible(statusLabel);
  addAndMakeVisible(targetPathLabel);

  selectAllButton.addListener(this);
  deselectAllButton.addListener(this);
  generateButton.addListener(this);

  statusLabel.setJustificationType(juce::Justification::centredLeft);
  statusLabel.setFont(juce::Font(13.0f));

  targetPathLabel.setJustificationType(juce::Justification::centredLeft);
  targetPathLabel.setFont(juce::Font(12.0f));
  targetPathLabel.setColour(juce::Label::textColourId, juce::Colours::grey);

  // Show target path
  juce::File doricoPath = generator.getDoricoBasePath();
  if (doricoPath != juce::File()) {
    targetPathLabel.setText("Target: " + doricoPath.getFullPathName(),
                            juce::dontSendNotification);
  } else {
    targetPathLabel.setText("Target: ~/Library/Application "
                            "Support/Steinberg/Dorico 6/ (will be created)",
                            juce::dontSendNotification);
  }

  buildInstrumentList();
}

DoricoSetupComponent::~DoricoSetupComponent() {
  selectAllButton.removeListener(this);
  deselectAllButton.removeListener(this);
  generateButton.removeListener(this);
}

// ── Build Instrument List ──────────────────────────────────────────────────

void DoricoSetupComponent::buildInstrumentList() {
  auto instruments = getDefaultInstruments();
  InstrumentCategory currentCategory = (InstrumentCategory)-1;

  int y = kSectionGap;

  for (size_t i = 0; i < instruments.size(); ++i) {
    const auto &instr = instruments[i];

    // Add category header if we're entering a new category
    if (instr.category != currentCategory) {
      currentCategory = instr.category;

      auto label = std::make_unique<juce::Label>();
      label->setText(categoryToString(currentCategory),
                     juce::dontSendNotification);
      label->setFont(juce::Font(15.0f).boldened());
      label->setColour(juce::Label::textColourId,
                       juce::Colour(0xff03dac6)); // Teal accent
      label->setBounds(kLeftMargin, y, 400, kCategoryHeaderHeight);
      contentComponent->addAndMakeVisible(label.get());
      categoryLabels.push_back(std::move(label));

      y += kCategoryHeaderHeight;
    }

    // Add instrument toggle
    auto toggle = std::make_unique<juce::ToggleButton>(instr.commonName);
    toggle->setBounds(kCheckboxIndent, y, 350, kRowHeight);
    toggle->setToggleState(true,
                           juce::dontSendNotification); // Default: all selected
    contentComponent->addAndMakeVisible(toggle.get());
    instrumentToggles.push_back(std::move(toggle));

    y += kRowHeight;
  }

  y += kSectionGap;
  contentComponent->setSize(500, y);
}

// ── Button Handling ────────────────────────────────────────────────────────

void DoricoSetupComponent::buttonClicked(juce::Button *button) {
  if (button == &selectAllButton) {
    selectAll(true);
  } else if (button == &deselectAllButton) {
    selectAll(false);
  } else if (button == &generateButton) {
    onGenerateClicked();
  }
}

void DoricoSetupComponent::selectAll(bool shouldBeSelected) {
  for (auto &toggle : instrumentToggles) {
    toggle->setToggleState(shouldBeSelected, juce::dontSendNotification);
  }
}

void DoricoSetupComponent::onGenerateClicked() {
  std::cerr << "[DoricoSetup] Generate button clicked" << std::endl;

  // Collect selected instrument indices
  std::vector<int> selectedIndices;
  for (size_t i = 0; i < instrumentToggles.size(); ++i) {
    if (instrumentToggles[i]->getToggleState()) {
      selectedIndices.push_back((int)i);
    }
  }

  std::cerr << "[DoricoSetup] Selected " << selectedIndices.size()
            << " instruments" << std::endl;

  if (selectedIndices.empty()) {
    statusLabel.setText("No instruments selected.", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId,
                          juce::Colour(0xffcf6679)); // Error red
    return;
  }

  juce::File baseDir = generator.getDoricoBasePath();
  std::cerr << "[DoricoSetup] Base dir: " << baseDir.getFullPathName()
            << std::endl;

  juce::String fileList =
      baseDir
          .getChildFile(
              "PlaybackTemplateGenerators/Fiddle/playbacktemplategen.xml")
          .getFullPathName() +
      "\n" +
      baseDir.getChildFile("PluginPresetLibraries/Fiddle/presets.xml")
          .getFullPathName() +
      "\n" +
      baseDir
          .getChildFile(
              "PluginPresetLibraries/Fiddle/presets_for_instruments.xml")
          .getFullPathName() +
      "\n" +
      baseDir.getChildFile("DefaultLibraryAdditions/Fiddle_Universal.doricolib")
          .getFullPathName();

  std::cerr << "[DoricoSetup] Showing confirmation dialog..." << std::endl;

  auto options =
      juce::MessageBoxOptions()
          .withIconType(juce::MessageBoxIconType::QuestionIcon)
          .withTitle("Install Dorico Configuration")
          .withMessage("This will write the following files for " +
                       juce::String((int)selectedIndices.size()) +
                       " instruments:\n\n" + fileList +
                       "\n\nExisting files will be backed up (.bak). Continue?")
          .withButton("Install")
          .withButton("Cancel");

  juce::AlertWindow::showAsync(options, [this, selectedIndices](int result) {
    std::cerr << "[DoricoSetup] Dialog result: " << result << std::endl;

    if (result != 1)
      return; // 0 = cancel, 1 = install

    // Generate assignments
    auto assignments = generator.generateAssignments(selectedIndices);
    std::cerr << "[DoricoSetup] Generated " << assignments.size()
              << " assignments" << std::endl;

    // Generate and install files
    auto installResult = generator.generateAndInstallFiles(assignments);

    if (installResult.wasOk()) {
      statusLabel.setText(
          "Success! Installed " + juce::String((int)assignments.size()) +
              " instruments. Restart Dorico to pick up changes.",
          juce::dontSendNotification);
      statusLabel.setColour(juce::Label::textColourId,
                            juce::Colour(0xff03dac6)); // Success teal

      auto instruments = getDefaultInstruments();
      std::cerr << "[DoricoSetup] Installed " << assignments.size()
                << " instrument assignments:" << std::endl;
      for (const auto &a : assignments) {
        const auto &instr = instruments[(size_t)a.instrumentIndex];
        std::cerr << "  " << instr.presetName << " -> Program " << a.program
                  << " Bank " << a.bankMSB << "/" << a.bankLSB << std::endl;
      }
    } else {
      std::cerr << "[DoricoSetup] ERROR: " << installResult.getErrorMessage()
                << std::endl;
      statusLabel.setText("Error: " + installResult.getErrorMessage(),
                          juce::dontSendNotification);
      statusLabel.setColour(juce::Label::textColourId,
                            juce::Colour(0xffcf6679)); // Error red
    }
  });
}

// ── Layout ─────────────────────────────────────────────────────────────────

void DoricoSetupComponent::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xff1e1e1e)); // Dark background

  // Draw a subtle separator above the button bar
  int buttonBarTop = getHeight() - kButtonBarHeight;
  g.setColour(juce::Colour(0xff333333));
  g.drawHorizontalLine(buttonBarTop, 0.0f, (float)getWidth());
}

void DoricoSetupComponent::resized() {
  auto bounds = getLocalBounds();
  int buttonBarHeight = kButtonBarHeight;

  // Viewport takes the main area
  auto viewportArea =
      bounds.removeFromTop(bounds.getHeight() - buttonBarHeight);
  viewport.setBounds(viewportArea);

  // Resize the content component width to match viewport
  contentComponent->setSize(viewportArea.getWidth() - 20,
                            contentComponent->getHeight());

  // Button bar at the bottom
  auto buttonBar = bounds;
  buttonBar.reduce(10, 5);

  // Top row: target path
  targetPathLabel.setBounds(buttonBar.removeFromTop(18));

  // Middle row: Select All / Deselect All / Generate
  auto buttonRow = buttonBar.removeFromTop(30);
  selectAllButton.setBounds(buttonRow.removeFromLeft(100));
  buttonRow.removeFromLeft(5);
  deselectAllButton.setBounds(buttonRow.removeFromLeft(100));
  buttonRow.removeFromLeft(10);
  generateButton.setBounds(buttonRow);

  // Status label
  statusLabel.setBounds(buttonBar);
}

} // namespace fiddle
