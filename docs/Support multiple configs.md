# Fiddle should support multiple configs

Currently, Fiddle saves its configuration and restores it when it is launched.  We want to add support for multiple configurations. We want to be able to save and restore multiple configurations. 

## Desired behavior

Fiddle should keep track of recently used configurations. When Fiddle is launched, it should display a dialog that allows the user to select from those configurations, or to select a configuration via a file browser, or to create a new configuration (giving it a name). If the user selects a configuration, it should be loaded. If the user creates a new configuration, it should be loaded. If the user cancels, Fiddle should close.

When Dorico loads a project that contains Fiddle VST3 plugins, it should load the configuration that was active when the project was saved. If the configuration is not found, it should load the default configuration.

The Dorico plugin currently has a file browser interface that allows the user to select a configuration file. This should be replaced with the interface described above. The Fiddle Plugin should display the config path, but not allow the user to change it. 


