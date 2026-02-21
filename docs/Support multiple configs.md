# Fiddle should support multiple configs

Currently, Fiddle saves its configuration and restores it when it is launched.  We want to add support for multiple configurations. We want to be able to save and restore multiple configurations. 

## Desired behavior

Fiddle should keep track of recently used configurations. When Fiddle is launched, it should display a dialog that allows the user to select from those configurations, or to select a configuration via a file browser, or to create a new configuration (giving it a name). If the user selects a configuration, it should be loaded. If the user creates a new configuration, it should be loaded. If the user cancels, Fiddle should close.

When Dorico loads a project that contains Fiddle VST3 plugins, it should load the configuration that was active when the project was saved. If the configuration is not found, it should load the default configuration.

The Dorico plugin currently has a file browser interface that allows the user to select a configuration file. This should be replaced with the interface described above. The Fiddle Plugin should display the config path, but not allow the user to change it. 


## Addendum

I've noticed an unforeseen interaction between the plugin and the server:

* If the user starts the server before starting Dorico, the server will not be connected. That's perfectly fine --- sometimes the user will want to work on a configuration without Dorico running. But sometimes the user will want to start the server, then start Dorico and have the server immediately load the configuration that is active in Dorico. So I think it might be better to have the server come up in a state where it is ready to load a configuration specified by the plugin. Maybe display "Waiting for connection" or similar. If the user wants to work on a configuration without Dorico running, they can do that by starting the server and then using the menu to load the configuration in the server UI. If Dorico subsequently connects, the server should give the user options: 

* load the configuration that is active in Dorico. If the user chooses this option, the server should save the config the user is working on and then replace it with the config from Dorico. 

* continue with the current configuration. If the user chooses this option, the server should continue with the current configuration. The current config should be transmitted to the plugin and displayed.

* reject the current connection request. The server should continue with the current configuration. The plugin should disconnect and display an affordance to reconnect. 




