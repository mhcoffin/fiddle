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

