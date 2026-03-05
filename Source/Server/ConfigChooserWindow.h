#pragma once

#include "FiddleDatabase.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace fiddle {

/// Content component for the config chooser dialog.
/// Shows a list of saved configs from the SQLite database.
class ConfigChooserComponent : public juce::Component,
                               public juce::ListBoxModel {
public:
  std::function<void(juce::String)> onConfigSelected;
  std::function<void()> onCancelled;

  ConfigChooserComponent(FiddleDatabase &db) : db_(db) {
    refreshList();

    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Fiddle - Select Configuration",
                       juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    // Config list label
    addAndMakeVisible(listLabel);
    listLabel.setText("Saved Configurations", juce::dontSendNotification);
    listLabel.setFont(juce::FontOptions(13.0f));
    listLabel.setColour(juce::Label::textColourId, juce::Colours::grey);

    // Config list
    addAndMakeVisible(configListBox);
    configListBox.setModel(this);
    configListBox.setRowHeight(32);
    configListBox.setColour(juce::ListBox::backgroundColourId,
                            juce::Colours::black.withAlpha(0.3f));

    // Buttons
    addAndMakeVisible(newConfigButton);
    newConfigButton.setButtonText("New Config");
    newConfigButton.onClick = [this]() { showNewConfigDialog(); };

    addAndMakeVisible(cancelButton);
    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this]() {
      if (onCancelled)
        onCancelled();
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

  void refreshList() {
    configs_ = db_.listConfigs();
    if (configListBox.isShowing())
      configListBox.updateContent();
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colour(0xFF1E1E2E)); // Dark background
  }

  void resized() override {
    auto bounds = getLocalBounds().reduced(20);

    titleLabel.setBounds(bounds.removeFromTop(36));
    bounds.removeFromTop(10);

    // List label
    listLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(4);

    // List takes up the bulk of the space
    auto buttonRow = bounds.removeFromBottom(40);
    auto newConfigRow = bounds.removeFromBottom(35);
    bounds.removeFromBottom(5);
    configListBox.setBounds(bounds);

    // New config row
    if (newNameEditor.isVisible()) {
      newNameEditor.setBounds(
          newConfigRow.removeFromLeft(newConfigRow.getWidth() - 80));
      newConfigRow.removeFromLeft(5);
      createButton.setBounds(newConfigRow);
    }

    // Bottom button row
    int buttonW = (buttonRow.getWidth() - 10) / 2;
    newConfigButton.setBounds(buttonRow.removeFromLeft(buttonW));
    buttonRow.removeFromLeft(10);
    cancelButton.setBounds(buttonRow);
  }

  // ListBoxModel
  int getNumRows() override { return (int)configs_.size(); }

  void paintListBoxItem(int row, juce::Graphics &g, int width, int height,
                        bool isSelected) override {
    if (row < 0 || row >= (int)configs_.size())
      return;

    if (isSelected)
      g.fillAll(juce::Colour(0xFF3D3D5C));
    else if (row % 2 == 0)
      g.fillAll(juce::Colour(0xFF2A2A3E));

    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(configs_[row].name, 10, 0, width / 2, height,
               juce::Justification::centredLeft);

    // Show version timestamp on the right
    g.setColour(juce::Colours::grey);
    g.setFont(10.0f);
    g.drawText(configs_[row].version, 10, 0, width - 20, height,
               juce::Justification::centredRight);
  }

  void listBoxItemDoubleClicked(int row, const juce::MouseEvent &) override {
    if (row >= 0 && row < (int)configs_.size()) {
      if (onConfigSelected)
        onConfigSelected(configs_[row].name);
    }
  }

private:
  FiddleDatabase &db_;
  std::vector<SavedConfigInfo> configs_;
  juce::Label titleLabel;
  juce::Label listLabel;
  juce::ListBox configListBox;
  juce::TextButton newConfigButton, cancelButton, createButton;
  juce::TextEditor newNameEditor;

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

    if (onConfigSelected)
      onConfigSelected(name);
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfigChooserComponent)
};

/// Window wrapper for the config chooser component.
class ConfigChooserWindow : public juce::DocumentWindow {
public:
  std::function<void(juce::String)> onConfigSelected;
  std::function<void()> onCancelled;

  ConfigChooserWindow(FiddleDatabase &db)
      : DocumentWindow("Fiddle", juce::Colour(0xFF1E1E2E),
                       DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);

    auto *content = new ConfigChooserComponent(db);
    content->onConfigSelected = [this](juce::String name) {
      if (onConfigSelected)
        onConfigSelected(name);
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
      content->refreshList();
  }

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfigChooserWindow)
};

} // namespace fiddle
