# SQLite

We will use SQLite to store the state of the Fiddle server. 

## Step 1


Create a C++ class FiddleDatabase that wraps a sqlite3 instance.

Implement a schema with two tables: CurrentSession (storing the current topology and plugin blobs) and SavedConfigs (named configurations for the user library). Saved configurations will be versioned. The version will be a timestamp. A new version will be created whenever the user explicitly saves the configuration. A new version will also be created if the user has changed the configuration (i.e., the config is dirty) when Dorico requests a new version. For now, loading a config will load the latest version. We will want to add a version selector in the future.

Use WAL (Write-Ahead Logging) mode for performance.

Implement a thread-safe method to update a plugin's binary state (BLOB) by its ID. This update must happen on a background task (using juce::ThreadPool or similar) so the UI and Audio threads are never blocked.

Ensure we use prepared statements for efficiency.

## Step 2

Now, implement a 'Shadow State' manager for Fiddle to handle getStateInformation requests from Dorico instantly.

Create a StateManager class that maintains a 'Global Binary Blob'.

Use a Triple-Buffer or Atomic Swap pattern: while the background thread is rebuilding the full project state (after a change), the host can still safely return the last 'complete' blob.

Implement a serialization routine that concatenates the metadata (config name, dirty flags) and all active plugin BLOBs into a single juce::MemoryBlock.

This routine should be triggered by the FiddleDatabase background task every time a 'significant' change is committed to the DB, ensuring the 'Shadow State' is always ready to be handed to Dorico.

When Dorico restores a project, it will send a blob to Fiddle, as well as the name of the SavedConfig it is based on. Fiddle should use this to update the CurrentSession table. If the SavedConfig is not found, Fiddle should create a new one with the name that Dorico sent.

## Scenarios

The complete identifier for a config consists of a name plus a version. The version is a timestamp.


The Fiddle plugin should display the name and version of the current config. The version should be displayed in date/time format. The plugin should display the name and version in green if the plugin is connected to the server, and in red if the plugin is not connected to the server.

### Scenario 1

The Fiddle server is running but Dorico is not. Then the user starts a fresh project using the Fiddle playback template, which causes the Fiddle plugin to be instantiated. 

1. The plugin should request the current config and version from the server. 

The result is that the Server configuration will not change. The plugin will connect and display the new config and version in green. I.e., MyConfig and time2.

### Scenario 2

The Fiddle server is not running. The user adds the Fiddle plugin to a Dorico project, but the plugin is not connected to the server. 

1. The plugin should display "No Configuration" in red.

### Scenario 2a

After scenario 2, the user starts the Fiddle server.

1. The server should load the configuration it was running when it was shut down.

2. It should report the config and version to the plugin.

3. The plugin should display the config and version in green.

### Scenario 3

The server is running but Dorico is not, so no plugin is connected to the server. Then Dorico is started and the user opens a project that was saved with a Fiddle config.

Here is what should happen in this scenario:

1. The server should save its current state to the database if the state is dirty. This will create a new version of the current config. E.g., if the current config is "MyConfig" and the version is time1, and the state is dirty, then the server should save the state to the database and create a new version of "MyConfig" with version time2. 

2. The server should accept the new config from Dorico. 

- If the config from Dorico has a name that does not exist in the database, the server should create a new config with that name with a single version that (the version that Dorico sent). 

- If the config from Dorico has a name that does exist in the database, but the version does not exist, then the version from Dorico should be added to the database. Note that the version from Dorico might be older than the latest version in the database. That's okay --- it just means that the Dorico project will not be using the latest version of the config. (There will be UI affordances in the future that allows the user to update to use the latest version of the config.)

The result should be that the server is updated to match the state when Dorico was last saved. The server should be in the same state as it would be if Dorico had been running the whole time.

### Scenario 4

Dorico is started and a project is opened that was saved with a Fiddle config. However, the server is not running.

Then the server is started. Here is what should happen:

1. The plugin should connect to the Server and send the config and version that was saved in the Dorico project.

2. The server should load the config that Dorico specifies via the plugin. 

- If the config from Dorico has a name that does not exist in the database, the server should create a new config with  specified name and a single version (the version that Dorico sent). 

- If the config from Dorico has a name that does exist in the database, but the version does not exist, then the version from Dorico should be added to the database. 

Note that Scenarios 3 and 4 converge to the same state. Also note that I've changed the spec so that Dorico always wins if there is a conflict. 


### Scenario 5

Dorico and the server are both running, and the config in the server matches the config in the plugin. (Steady state.) Then the user modifies something in the server. E.g., loads a new library for an instrument, or adjusts the gain on a strip. 

1. In the server, the active config should be updated and marked as dirty. And the timestamp of the active config should be updated to the current time. A "Save" button at the top of the mixer panel should indicate that the config is dirty.

2. An indicator in the plugin should indicate that the config is dirty. The plugin should display a dirty indicator of some sort.

### Scenario 5a

After scenario 5, the user clicks "Save". 

1. The server should save the current config to the database as a new version. 

2. The save button should indicate that the config is no longer dirty and be disabled.

3. The new version should be propagated to the plugin and displayed in the plugin. The dirty indicator in the plugin should be cleared.

### Scenario 5b

After scenario 5, while the dirty bit is on, Dorico starts to save the project. 

1. As part of that save, Dorico calls `getStateInformation` on the plugin. 

2. The plugin reads the already-current blob from the shared file. The server keeps this file up-to-date via `scheduleStateRebuild` after every change, so it already reflects the latest state including the dirty changes.

3. The blob (containing config name, version, strip data, and plugin states) is returned to Dorico.

4. The server saves the current config to the database as a new version and clears the dirty indicators, both in the server and the plugin.

5. Dorico saves the state information.

### Scenario 6

The user creates a brand-new config from the server UI.

1. All strips are cleared, a new empty config is created with the given name and version=now.

### Note

If the user never click "Save" in the FiddleServer mixer, then save will only happen when Dorico saves a project.

