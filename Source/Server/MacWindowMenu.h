#pragma once

namespace fiddle::mac_window_menu {

// Register the application's native Window menu with AppKit. In addition to
// maintaining the list of open windows, AppKit augments this menu with its
// standard window-management commands (including Move & Resize).
void install();
void uninstall();

} // namespace fiddle::mac_window_menu
