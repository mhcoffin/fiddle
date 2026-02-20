#pragma once

#include "FiddleConfig.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace fiddle {

/// Content component for the config chooser dialog.
/// Shows a list of recent configs plus browse / new / cancel actions.
class ConfigChooserComponent : public juce::Component,
                               public juce::ListBoxModel {
public:
  std::function<void(juce::File)> onConfigSelected;
  std::function<void()> onCancelled;

  ConfigChooserComponent() {
    refreshRecentList();

    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Fiddle - Select Configuration",
                       juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    // Recent list
    addAndMakeVisible(recentListBox);
    recentListBox.setModel(this);
    recentListBox.setRowHeight(32);
    recentListBox.setColour(juce::ListBox::backgroundColourId,
                            juce::Colours::black.withAlpha(0.3f));

    // Buttons
    addAndMakeVisible(browseButton);
    browseButton.setButtonText("Browse...");
    browseButton.onClick = [this]() { browseForConfig(); };

    addAndMakeVisible(newConfigButton);
    newConfigButton.setButtonText("New Config");
    newConfigButton.onClick = [this]() { showNewConfigDialog(); };

    addAndMakeVisible(cancelButton);
    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this]() {
      if (onCancelled)
        onCancelled();
    };

    addAndMakeVisible(waitButton);
    waitButton.setButtonText("Wait for Dorico");
    waitButton.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(0xFF2D5F2D));
    waitButton.onClick = [this]() {
      // Return empty file = waiting state
      if (onConfigSelected)
        onConfigSelected(juce::File());
    };

    // New config name field (initially hidden)
    addChildComponent(newNameEditor);
    newNameEditor.setTextToShowWhenEmpty("Enter config name...",
                                         juce::Colours::grey);
    newNameEditor.onReturnKey = [this]() { createConfigFromEditor(); };

    addChildComponent(createButton);
    createButton.setButtonText("Create");
    createButton.onClick = [this]() { createConfigFromEditor(); };

    setSize(450, 400);
  }

  void refreshRecentList() {
    recentPaths = FiddleConfig::loadRecentConfigs();
    if (recentListBox.isShowing())
      recentListBox.updateContent();
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colour(0xFF1E1E2E)); // Dark background
  }

  void resized() override {
    auto bounds = getLocalBounds().reduced(20);

    titleLabel.setBounds(bounds.removeFromTop(36));
    bounds.removeFromTop(10);

    // Recent label
    auto recentArea = bounds.removeFromTop(20);
    // Note: we don't add a separate label, but this reserves space

    // List takes up the bulk of the space
    auto buttonRow = bounds.removeFromBottom(40);
    auto newConfigRow = bounds.removeFromBottom(35);
    bounds.removeFromBottom(5);
    recentListBox.setBounds(bounds);

    // New config row
    if (newNameEditor.isVisible()) {
      newNameEditor.setBounds(
          newConfigRow.removeFromLeft(newConfigRow.getWidth() - 80));
      newConfigRow.removeFromLeft(5);
      createButton.setBounds(newConfigRow);
    }

    // Bottom button row (4 buttons)
    int buttonW = (buttonRow.getWidth() - 30) / 4;
    browseButton.setBounds(buttonRow.removeFromLeft(buttonW));
    buttonRow.removeFromLeft(10);
    newConfigButton.setBounds(buttonRow.removeFromLeft(buttonW));
    buttonRow.removeFromLeft(10);
    waitButton.setBounds(buttonRow.removeFromLeft(buttonW));
    buttonRow.removeFromLeft(10);
    cancelButton.setBounds(buttonRow);
  }

  // ListBoxModel
  int getNumRows() override { return recentPaths.size(); }

  void paintListBoxItem(int row, juce::Graphics &g, int width, int height,
                        bool isSelected) override {
    if (row < 0 || row >= recentPaths.size())
      return;

    if (isSelected)
      g.fillAll(juce::Colour(0xFF3D3D5C));
    else if (row % 2 == 0)
      g.fillAll(juce::Colour(0xFF2A2A3E));

    g.setColour(juce::Colours::white);
    g.setFont(14.0f);

    juce::File f(recentPaths[row]);
    juce::String displayName = f.getFileNameWithoutExtension();

    g.drawText(displayName, 10, 0, width - 20, height,
               juce::Justification::centredLeft);

    // Show full path in smaller text on the right
    g.setColour(juce::Colours::grey);
    g.setFont(10.0f);
    g.drawText(f.getParentDirectory().getFullPathName(), 10, 0, width - 20,
               height, juce::Justification::centredRight);
  }

  void listBoxItemDoubleClicked(int row, const juce::MouseEvent &) override {
    if (row >= 0 && row < recentPaths.size()) {
      juce::File f(recentPaths[row]);
      if (f.existsAsFile()) {
        FiddleConfig::saveRecentConfig(f);
        if (onConfigSelected)
          onConfigSelected(f);
      }
    }
  }

private:
  juce::Label titleLabel;
  juce::ListBox recentListBox;
  juce::TextButton browseButton, newConfigButton, cancelButton, createButton,
      waitButton;
  juce::TextEditor newNameEditor;
  juce::StringArray recentPaths;
  std::unique_ptr<juce::FileChooser> fileChooser;

  void browseForConfig() {
    auto startDir = FiddleConfig::getConfigDir();
    fileChooser = std::make_unique<juce::FileChooser>("Select Fiddle Config...",
                                                      startDir, "*.yaml", true);

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser &chooser) {
                               auto result = chooser.getResult();
                               if (result.existsAsFile()) {
                                 FiddleConfig::saveRecentConfig(result);
                                 if (onConfigSelected)
                                   onConfigSelected(result);
                               }
                             });
  }

  void showNewConfigDialog() {
    newNameEditor.setVisible(true);
    createButton.setVisible(true);
    newNameEditor.grabKeyboardFocus();
    resized();
  }

  void createConfigFromEditor() {
    auto name = newNameEditor.getText().trim();
    if (name.isEmpty())
      return;

    auto file = FiddleConfig::createNewConfig(name);
    FiddleConfig::saveRecentConfig(file);
    if (onConfigSelected)
      onConfigSelected(file);
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfigChooserComponent)
};

/// Window wrapper for the config chooser component.
class ConfigChooserWindow : public juce::DocumentWindow {
public:
  std::function<void(juce::File)> onConfigSelected;
  std::function<void()> onCancelled;

  ConfigChooserWindow()
      : DocumentWindow("Fiddle", juce::Colour(0xFF1E1E2E),
                       DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);

    auto *content = new ConfigChooserComponent();
    content->onConfigSelected = [this](juce::File f) {
      if (onConfigSelected)
        onConfigSelected(f);
    };
    content->onCancelled = [this]() {
      if (onCancelled)
        onCancelled();
    };

    setContentOwned(content, true);
    centreWithSize(getWidth(), getHeight());
    setResizable(false, false);
    setVisible(true);
  }

  void closeButtonPressed() override {
    if (onCancelled)
      onCancelled();
  }

  void refresh() {
    if (auto *content =
            dynamic_cast<ConfigChooserComponent *>(getContentComponent()))
      content->refreshRecentList();
  }

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfigChooserWindow)
};

} // namespace fiddle
