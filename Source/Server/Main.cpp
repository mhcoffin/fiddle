#include "ConfigChooserWindow.h"
#include "FiddleConfig.h"
#include "MainComponent.h"
#include <juce_gui_extra/juce_gui_extra.h>

#include <JuceHeader.h>

namespace fiddle {

class FiddleServerApplication : public juce::JUCEApplication,
                                public juce::MenuBarModel {
public:
  FiddleServerApplication() {}

  const juce::String getApplicationName() override { return "FiddleServer"; }
  const juce::String getApplicationVersion() override {
    return ProjectInfo::versionString;
  }
  bool moreThanOneInstanceAllowed() override { return true; }

  class MainWindow : public juce::DocumentWindow {
  public:
    MainWindow(juce::String name, const juce::File &configFile)
        : DocumentWindow(
              name,
              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                  juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new MainComponent(configFile), true);

#if JUCE_IOS || JUCE_ANDROID
      setFullScreen(true);
#else
      setResizable(true, true);
      centreWithSize(getWidth(), getHeight());
#endif

      setVisible(true);
    }

    void closeButtonPressed() override {
      juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

  void initialise(const juce::String &commandLine) override {
    // Migrate legacy config if needed
    FiddleConfig::migrateLegacyConfig();

    // Start immediately in waiting mode — TCP server runs, ready for Dorico.
    // User can load a config manually via File > Open Config.
    openConfig(juce::File());
  }

  void shutdown() override {
    configChooser.reset();
    mainWindow.reset();
  }

  void systemRequestedQuit() override { quit(); }

  void anotherInstanceStarted(const juce::String &commandLine) override {}

  // ── Menu Bar ──────────────────────────────────────────────

  juce::StringArray getMenuBarNames() override { return {"File"}; }

  juce::PopupMenu getMenuForIndex(int menuIndex,
                                  const juce::String &menuName) override {
    juce::PopupMenu menu;
    if (menuIndex == 0) {
      menu.addItem(1, "Open Config...");
      menu.addItem(2, "New Config...");
      menu.addSeparator();
      menu.addItem(3, "Save Config  (Cmd+S)");
      menu.addItem(4, "Save As...");
    }
    return menu;
  }

  void menuItemSelected(int menuItemID, int topLevelMenuIndex) override {
    if (menuItemID == 1) {
      showConfigChooser(false);
    } else if (menuItemID == 2) {
      showNewConfigDialog();
    } else if (menuItemID == 3) {
      saveCurrentConfig();
    } else if (menuItemID == 4) {
      showSaveAsDialog();
    }
  }

  // ── Config Management ─────────────────────────────────────

  void showConfigChooser(bool isLaunch) {
    configChooser = std::make_unique<ConfigChooserWindow>();
    configChooser->onConfigSelected = [this](juce::File f) {
      openConfig(f);
      configChooser.reset();
    };
    configChooser->onCancelled = [this, isLaunch]() {
      configChooser.reset();
      if (isLaunch)
        quit();
    };
  }

  void openConfig(const juce::File &configFile) {
    // Save current config before switching (only if one was loaded)
    if (mainWindow && activeConfigFile.existsAsFile()) {
      saveCurrentConfig();
    }
    // Always destroy old MainWindow (and its MidiTcpServer) before creating
    // the new one, so the TCP listener socket on port 5252 is released.
    mainWindow.reset();

    activeConfigFile = configFile;

    if (configFile.existsAsFile()) {
      // Normal config load
      FiddleConfig::saveRecentConfig(configFile);
      FiddleConfig::writeActiveConfig(configFile);
      mainWindow = std::make_unique<MainWindow>(
          getApplicationName() + " - " +
              configFile.getFileNameWithoutExtension(),
          configFile);
    } else {
      // Waiting state — no config loaded, listen for plugin connection
      mainWindow = std::make_unique<MainWindow>(
          getApplicationName() + " - Waiting for connection", juce::File());
    }

    // Set menu bar on macOS
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
#endif

    // Listen for config changes from MainComponent (e.g. auto-loaded from
    // plugin)
    if (auto *mc =
            dynamic_cast<MainComponent *>(mainWindow->getContentComponent())) {
      mc->onConfigChanged = [this](const juce::File &newConfig) {
        activeConfigFile = newConfig;
        mainWindow->setName(getApplicationName() + " - " +
                            newConfig.getFileNameWithoutExtension());
      };
    }
  }

  void saveCurrentConfig() {
    if (mainWindow) {
      if (auto *mc = dynamic_cast<MainComponent *>(
              mainWindow->getContentComponent())) {
        mc->saveConfig();
      }
    }
  }

  void showNewConfigDialog() {
    auto *aw = new juce::AlertWindow("New Configuration",
                                     "Enter a name for the new configuration:",
                                     juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", "", "Config name:");
    aw->addButton("Create", 1);
    aw->addButton("Cancel", 0);
    aw->enterModalState(
        true, juce::ModalCallbackFunction::create([this, aw](int result) {
          if (result == 1) {
            auto name = aw->getTextEditorContents("name").trim();
            if (name.isNotEmpty()) {
              auto file = FiddleConfig::createNewConfig(name);
              openConfig(file);
            }
          }
          delete aw;
        }),
        true);
  }
  void showSaveAsDialog() {
    auto *aw = new juce::AlertWindow("Save Configuration As",
                                     "Enter a name for the configuration:",
                                     juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", "", "Config name:");
    aw->addButton("Save", 1);
    aw->addButton("Cancel", 0);
    aw->enterModalState(
        true, juce::ModalCallbackFunction::create([this, aw](int result) {
          if (result == 1) {
            auto name = aw->getTextEditorContents("name").trim();
            if (name.isNotEmpty()) {
              auto file =
                  FiddleConfig::getConfigDir().getChildFile(name + ".yaml");
              if (mainWindow) {
                if (auto *mc = dynamic_cast<MainComponent *>(
                        mainWindow->getContentComponent())) {
                  mc->saveConfigAs(file);
                }
                activeConfigFile = file;
                mainWindow->setName(getApplicationName() + " - " + name);
              }
            }
          }
          delete aw;
        }),
        true);
  }

private:
  std::unique_ptr<MainWindow> mainWindow;
  std::unique_ptr<ConfigChooserWindow> configChooser;
  juce::File activeConfigFile;
};

} // namespace fiddle

START_JUCE_APPLICATION(fiddle::FiddleServerApplication)
