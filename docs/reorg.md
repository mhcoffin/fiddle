# Reorganizing the UI

The UI is getting a bit unwieldy, so we're going to reorganize it.

The biggest concern is that the Mixer tab is by far the most used part of the UI, but the other tabs have equal prominence. In addition, the user is likely to want to view the mixer while looking at the Time Line and the Event Log. This is currently not possible.

## Proposal

When the app starts, it will show first the splash screen. Then it will show either the contents of what is now the Mixer tab (`mixer mode`) or the contents of the Setup tab (`setup mode`). This is the "main" window. 
The choice of mode depends on the last time the app was run. However, *if* the app has not been run before (i.e, there is no entry in the database for the last view), it will start in `setup mode`.

There will be an affordance in mixer mode to switch to setup mode. The affordance will be an edit icon button in the top right of the main window. 

Setup mode should have two buttons to exit that mode and go back to mixer mode: `Cancel` and `Save`. 
The `Save` button should write .doricolib and expression map XML to the Dorico Library folder. it should also sync Mixer strips to the new ensemble. Mixer strips that are not present in the new ensemble should be removed. 
Mixer strips that are present in the new ensemble but not in the old ensemble should be added. 
Any mixer strips that are present both before and after should retain their settings (volume, mute, solo, etc.)
The `Cancel` button should return to the Mixer without doing anything.

The user can bring up a second independent WebView. This is the "debug" window. It will have tabs for the Time Line, Event Log, and Plugins. We may (later) add a tab for user settings. The app will add a `View` menu that will have an item with a toggle to show/hide the debug window. The toggle will be unchecked by default. Its value will be persisted in the SQLite database in a table called `user_settings`.

Both windows will be resizable and movable.  Their positions, sizes, and visibility, and mode (for the main window) will be persisted in the SQLite database in a table called `window_settings`. When the app starts, it will restore the positions and sizes and mode of the windows to the values stored in the database. If the position or size of a window is not visible on the screen, it will be restored to the default position and size.

Both windows should be created eagerly. Visibility of the debug window will be controlled by the `View` menu item. Visibility of the main window will be controlled by the OS window manager.

Give each WebView its own set of native functions, with a shared C++ layer underneath. 

The app should automatically do an incremental scan for plugins when it starts. 
The database should have a table that stores per-plugin information: path, modification time, valid flag, etc. 
Incremental scanning should use the information stored in the `plugin_cache` table to minimize the amount of work it does. It should not introspect or load the binary for plugins that are present in the cache. New plugins should be added to `plugin_cache`. Plugins that are no longer present should be removed. 

Plugins that are present in the filesystem but fail to load should be marked as "invalid" in the `plugin_cache` table. They should be shown in the Plugins tab with an indication that they are invalid. They should not be shown in the plugin selector. 

The main app menu should have a "Rescan" item that will trigger a full rescan of all plugin locations. It should discard the plugin cache and start fresh.  The same action should be available as a button in the Plugins tab.