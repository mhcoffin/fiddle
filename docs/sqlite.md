# SQLite

We will use SQLite to store the state of the Fiddle server. 

## Step 1


Create a C++ class FiddleDatabase that wraps a sqlite3 instance.

Implement a schema with two tables: CurrentSession (storing the current topology and plugin blobs) and SavedConfigs (named configurations for the user library).

Use WAL (Write-Ahead Logging) mode for performance.

Implement a thread-safe method to update a plugin's binary state (BLOB) by its ID. This update must happen on a background task (using juce::ThreadPool or similar) so the UI and Audio threads are never blocked.

Ensure we use prepared statements for efficiency.