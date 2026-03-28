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
    MainWindow(juce::String name)
        : DocumentWindow(
              name,
              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                  juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new MainComponent(), true);

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
    mainWindow = std::make_unique<MainWindow>(getApplicationName());

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
#endif
  }

  void shutdown() override {
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
    mainWindow.reset();
  }

  void systemRequestedQuit() override { quit(); }

  void anotherInstanceStarted(const juce::String &commandLine) override {}

  // ── Menu Bar ──────────────────────────────────────────────

  juce::StringArray getMenuBarNames() override { return {"File", "View"}; }

  juce::PopupMenu getMenuForIndex(int menuIndex,
                                  const juce::String &menuName) override {
    juce::PopupMenu menu;
    if (menuIndex == 0) {
      menu.addItem(3, "Save  (Cmd+S)");
    } else if (menuIndex == 1) {
      // View menu
      bool debugVisible = false;
      bool historyVisible = false;
      bool setupVisible = false;
      bool libraryMgrVisible = false;
      if (mainWindow) {
        if (auto *mc = dynamic_cast<MainComponent *>(
                mainWindow->getContentComponent())) {
          debugVisible = mc->isDebugWindowVisible();
          historyVisible = mc->isHistoryWindowVisible();
          setupVisible = mc->isSetupWindowVisible();
          libraryMgrVisible = mc->isLibraryManagerWindowVisible();
        }
      }
      menu.addItem(10, "Show Debug Window", true, debugVisible);
      menu.addItem(11, "Show History Window", true, historyVisible);
      menu.addItem(12, "Show Setup Window", true, setupVisible);
      menu.addItem(13, "Show Library Manager", true, libraryMgrVisible);
    }
    return menu;
  }

  void menuItemSelected(int menuItemID, int topLevelMenuIndex) override {
    if (menuItemID == 3) {
      saveCurrentState();
    } else if (menuItemID == 10) {
      // Toggle debug window
      if (mainWindow) {
        if (auto *mc = dynamic_cast<MainComponent *>(
                mainWindow->getContentComponent())) {
          mc->toggleDebugWindow();
        }
      }
    } else if (menuItemID == 11) {
      // Toggle history window
      if (mainWindow) {
        if (auto *mc = dynamic_cast<MainComponent *>(
                mainWindow->getContentComponent())) {
          mc->toggleHistoryWindow();
        }
      }
    } else if (menuItemID == 12) {
      // Toggle setup window
      if (mainWindow) {
        if (auto *mc = dynamic_cast<MainComponent *>(
                mainWindow->getContentComponent())) {
          mc->toggleSetupWindow();
        }
      }
    } else if (menuItemID == 13) {
      // Toggle library manager window
      if (mainWindow) {
        if (auto *mc = dynamic_cast<MainComponent *>(
                mainWindow->getContentComponent())) {
          mc->toggleLibraryManagerWindow();
        }
      }
    }
  }

  void saveCurrentState() {
    if (mainWindow) {
      if (auto *mc = dynamic_cast<MainComponent *>(
              mainWindow->getContentComponent())) {
        mc->saveConfig();
      }
    }
  }

private:
  std::unique_ptr<MainWindow> mainWindow;
};

} // namespace fiddle

START_JUCE_APPLICATION(fiddle::FiddleServerApplication)
