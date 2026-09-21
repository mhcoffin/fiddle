#include "ApplicationRestart.h"
#include "MainComponent.h"
#include "MacWindowMenu.h"
#include <juce_gui_extra/juce_gui_extra.h>

#include <JuceHeader.h>

namespace fiddle {

class FiddleServerApplication : public juce::JUCEApplication,
                                public juce::MenuBarModel {
public:
  static constexpr int kSaveMenuItemId = 3;
  static constexpr int kRestartMenuItemId = 20;
  static constexpr int kMainMixerMenuItemId = 101;
  static constexpr int kInspectorMenuItemId = 102;
  static constexpr int kAudioBankMenuItemId = 103;
  static constexpr int kLibraryManagerMenuItemId = 104;
  static constexpr int kHistoryMenuItemId = 105;
  static constexpr int kPrintMenuItemId = 106;
  static constexpr int kTechniqueCatalogMenuItemId = 107;
  static constexpr int kChairsMenuItemId = 111;
  static constexpr int kBusesMenuItemId = 112;
  static constexpr int kPluginsMenuItemId = 113;
  static constexpr int kSettingsMenuItemId = 114;
  static constexpr int kAudioPerformanceMenuItemId = 121;
  static constexpr int kTimelineMenuItemId = 122;
  static constexpr int kEventLogMenuItemId = 123;
  static constexpr int kMidiCaptureMenuItemId = 124;

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
    applicationMenu_.addItem(kRestartMenuItemId, "Restart Fiddle");
    juce::MenuBarModel::setMacMainMenu(this, &applicationMenu_);

    // setMacMainMenu() schedules one final asynchronous menu rebuild. Install
    // the native Window menu after that rebuild so JUCE does not replace it
    // with the empty placeholder returned by getMenuForIndex().
    auto *application = this;
    juce::MessageManager::callAsync([application] {
      if (juce::JUCEApplicationBase::getInstance() == application)
        mac_window_menu::install();
    });
#endif
  }

  void shutdown() override {
#if JUCE_MAC
    mac_window_menu::uninstall();
    juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
    mainWindow.reset();
  }

  void systemRequestedQuit() override { confirmLibraryDiscard([this] { quit(); }); }

  void anotherInstanceStarted(const juce::String &commandLine) override {}

  // ── Menu Bar ──────────────────────────────────────────────

  juce::StringArray getMenuBarNames() override {
#if JUCE_MAC
    return {"File", "View", "Window"};
#else
    return {"File", "View"};
#endif
  }

  juce::PopupMenu getMenuForIndex(int menuIndex,
                                  const juce::String &menuName) override {
    juce::PopupMenu menu;
    if (menuIndex == 0) {
      menu.addItem(kSaveMenuItemId, "Save  (Cmd+S)");
      menu.addSeparator();
      // Keep restart in the normal menu model as well as the macOS application
      // menu. AppKit may rebuild its application menu independently; File must
      // remain a reliable way to reach this command.
      menu.addItem(kRestartMenuItemId, "Restart Fiddle");
    } else if (menuIndex == 1) {
      juce::PopupMenu windows;
      windows.addItem(kMainMixerMenuItemId, "Main Mixer");
      windows.addItem(kPrintMenuItemId, "Print Audio...");
      windows.addItem(kChairsMenuItemId, "Chairs...");
      windows.addItem(kBusesMenuItemId, "Audio Bus Manager...");
      windows.addItem(kSettingsMenuItemId, "Settings...");
      windows.addItem(kLibraryManagerMenuItemId, "Library Manager");
      windows.addItem(kTechniqueCatalogMenuItemId, "Technique Catalog...");
      windows.addItem(kHistoryMenuItemId, "Project History");
      windows.addSeparator();
      windows.addItem(kAudioPerformanceMenuItemId, "Audio Performance");
      windows.addItem(kPluginsMenuItemId, "Plug-ins...");
      windows.addItem(kTimelineMenuItemId, "Timeline");
      windows.addItem(kEventLogMenuItemId, "Event Log");
      windows.addItem(kMidiCaptureMenuItemId, "MIDI Capture");
      menu.addSubMenu("Windows", windows);

      juce::PopupMenu layout;
      layout.addItem(kInspectorMenuItemId, "Layer Inspector");
      layout.addItem(kAudioBankMenuItemId, "Audio Buses & Master");
      menu.addSubMenu("Mixer layout", layout);
    }
    return menu;
  }

  void menuItemSelected(int menuItemID, int topLevelMenuIndex) override {
    if (menuItemID == kSaveMenuItemId) {
      saveCurrentState();
    } else if (menuItemID == kRestartMenuItemId) {
      restartApplication();
    } else if (const auto view = viewIdForMenuItem(menuItemID); view.isNotEmpty()) {
      if (mainWindow)
        if (auto *component = dynamic_cast<MainComponent *>(
                mainWindow->getContentComponent()))
          component->showView(view);
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

  void restartApplication() {
    confirmLibraryDiscard([this] { restartAfterConfirmation(); });
  }

  void restartAfterConfirmation() {
    if (restartPending_)
      return;

    const auto executable = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    if (!application_restart::schedule(executable)) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::MessageBoxIconType::WarningIcon, "Unable to Restart",
          "FiddleServer could not schedule its replacement process.");
      return;
    }

    restartPending_ = true;
    quit();
  }

private:
  static juce::String viewIdForMenuItem(int item) {
    switch (item) {
    case kMainMixerMenuItemId: return "main";
    case kInspectorMenuItemId: return "inspector";
    case kAudioBankMenuItemId: return "audio-bank";
    case kLibraryManagerMenuItemId: return "library-manager";
    case kHistoryMenuItemId: return "project-history";
    case kPrintMenuItemId: return "print";
    case kTechniqueCatalogMenuItemId: return "technique-catalog";
    case kChairsMenuItemId: return "chairs";
    case kBusesMenuItemId: return "bus-manager";
    case kPluginsMenuItemId: return "plugins";
    case kSettingsMenuItemId: return "settings";
    case kAudioPerformanceMenuItemId: return "audio-performance";
    case kTimelineMenuItemId: return "timeline";
    case kEventLogMenuItemId: return "event-log";
    case kMidiCaptureMenuItemId: return "midi-capture";
    default: return {};
    }
  }

  void confirmLibraryDiscard(std::function<void()> proceed) {
    auto *component = mainWindow ? dynamic_cast<MainComponent *>(mainWindow->getContentComponent()) : nullptr;
    if (!component || !component->hasUnsavedLibraryDraft()) { proceed(); return; }
    if (libraryQuitPromptPending_) return;
    libraryQuitPromptPending_ = true;
    juce::AlertWindow::showOkCancelBox(
      juce::MessageBoxIconType::WarningIcon, "Unsaved Library Changes",
      "The Library Manager has unsaved changes. Discard them and continue?",
      "Discard changes", "Keep editing", nullptr,
      juce::ModalCallbackFunction::create([this, proceed = std::move(proceed)](int result) {
        libraryQuitPromptPending_ = false;
        if (result != 0) proceed();
        else if (mainWindow)
          if (auto *component = dynamic_cast<MainComponent *>(mainWindow->getContentComponent()))
            component->showLibraryManagerWindow();
      }));
  }
  bool libraryQuitPromptPending_ = false;
  juce::PopupMenu applicationMenu_;
  std::unique_ptr<MainWindow> mainWindow;
  bool restartPending_ = false;
};

} // namespace fiddle

START_JUCE_APPLICATION(fiddle::FiddleServerApplication)
