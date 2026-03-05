#include "../TimeFormat.h"
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
    MainWindow(juce::String name, const juce::String &configName)
        : DocumentWindow(
              name,
              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                  juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new MainComponent(configName), true);

#if JUCE_IOS || JUCE_ANDROID
      setFullScreen(true);
#else
      setResizable(true, true);

      // Restore window geometry from database
      if (auto *mc = dynamic_cast<MainComponent *>(getContentComponent())) {
        auto bounds = mc->restoreMainWindowGeometry();
        setBounds(bounds);
      } else {
        centreWithSize(getWidth(), getHeight());
      }
#endif

      setVisible(true);
    }

    void closeButtonPressed() override {
      juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void moved() override {
      DocumentWindow::moved();
      saveGeometry();
    }

    void resized() override {
      DocumentWindow::resized();
      saveGeometry();
    }

  private:
    void saveGeometry() {
      if (auto *mc = dynamic_cast<MainComponent *>(getContentComponent())) {
        auto b = getBounds();
        mc->saveMainWindowGeometry(b.getX(), b.getY(), b.getWidth(),
                                   b.getHeight());
      }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

  void initialise(const juce::String &commandLine) override {
    // Open a DB connection for the app-level config chooser
    auto dbFile = FiddleConfig::getAppDataDir().getChildFile("fiddle.db");
    appDb_.open(dbFile);

    // Start in waiting mode — plugin connection will dictate which config
    openConfig({});
  }

  void shutdown() override {
    configChooser.reset();
    mainWindow.reset();
    appDb_.close();
  }

  void systemRequestedQuit() override { quit(); }

  void anotherInstanceStarted(const juce::String &commandLine) override {}

  // ── Menu Bar ──────────────────────────────────────────────

  juce::StringArray getMenuBarNames() override { return {"File", "View"}; }

  juce::PopupMenu getMenuForIndex(int menuIndex,
                                  const juce::String &menuName) override {
    juce::PopupMenu menu;
    if (menuIndex == 0) {
      menu.addItem(1, "Open Config...");
      menu.addItem(2, "New Config...");
      menu.addSeparator();
      menu.addItem(3, "Save Config  (Cmd+S)");
      menu.addItem(4, "Save As...");
    } else if (menuIndex == 1) {
      // View menu
      bool debugVisible = false;
      if (mainWindow) {
        if (auto *mc = dynamic_cast<MainComponent *>(
                mainWindow->getContentComponent())) {
          debugVisible = mc->isDebugWindowVisible();
        }
      }
      menu.addItem(10, "Show Debug Window", true, debugVisible);
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
    } else if (menuItemID == 10) {
      // Toggle debug window
      if (mainWindow) {
        if (auto *mc = dynamic_cast<MainComponent *>(
                mainWindow->getContentComponent())) {
          mc->toggleDebugWindow();
        }
      }
    }
  }

  // ── Config Management ─────────────────────────────────────

  void showConfigChooser(bool isLaunch) {
    configChooser = std::make_unique<ConfigChooserWindow>(appDb_);
    configChooser->onConfigSelected = [this](juce::String name) {
      openConfig(name);
      configChooser.reset();
    };
    configChooser->onCancelled = [this, isLaunch]() {
      configChooser.reset();
      if (isLaunch)
        quit();
    };
  }

  void openConfig(const juce::String &configName) {
    // Save current config before switching (only if one was loaded)
    if (mainWindow) {
      saveCurrentConfig();
    }
    // Always destroy old MainWindow (and its MidiTcpServer) before creating
    // the new one, so the TCP listener socket on port 5252 is released.
    mainWindow.reset();

    mainWindow = std::make_unique<MainWindow>(getApplicationName(), configName);

    // Set menu bar on macOS
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
#endif

    // Listen for config changes from MainComponent
    if (auto *mc =
            dynamic_cast<MainComponent *>(mainWindow->getContentComponent())) {
      mc->onConfigChanged = [this](const juce::String & /*name*/,
                                   const juce::String &version) {
        if (mainWindow) {
          juce::String title = getApplicationName();
          if (version.isNotEmpty())
            title += " @ " + fiddle::formatTimestamp(version);
          mainWindow->setName(title);
        }
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
              openConfig(name);
              // Clear all strips for a fresh start
              if (auto *mc = dynamic_cast<MainComponent *>(
                      mainWindow->getContentComponent())) {
                mc->clearForNewConfig();
              }
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
              if (mainWindow) {
                if (auto *mc = dynamic_cast<MainComponent *>(
                        mainWindow->getContentComponent())) {
                  mc->saveConfigAs(name);
                }
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
  FiddleDatabase appDb_;
};

} // namespace fiddle

START_JUCE_APPLICATION(fiddle::FiddleServerApplication)
