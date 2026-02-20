# Add VST host capability to Fiddle

## Overview

We want to add VST host capability to Fiddle. This will allow us to load VST plugins and drive them with MIDI. We want to use best practices for MIDI hosting to avoid the common pitfalls of VST hosting, such as crashes and hangs. 

## First Step

We will start by implementing scanning for VST plugins that are installed in standard locations on the system. We will use the JUCE VST wrapper to load the plugins. Please create a new tab in the Fiddle UI. It should have a button to scan for plugins and display the list of plugins. 

## Step 2

We will add a way to load a plugin and display its UI. For each plugin in the list, we will add a button to load it. When the plugin is loaded, we will display its UI in the plugin tab. 

## Step 3

We will add a new tab to hold the main UI for routing MIDI to the plugins, and routing audio from the plugins back into the main audio output. This will be the main "DAW" like interface for Fiddle. There will be several kinds of channels. In this step we will just implement a single kind of channel that routes incoming Note data structures to a plugin, and audio from the plugin back to Dorico. The channels will be vertical and will be arranged horizontally. There will be a button to add a new channel. 

Recall that the Fiddle Server is receiving MIDI events from the Fiddle VST3 plugin, which is installed in Dorico. We need to route audio from the Fiddle Server to the Fiddle plugin, and then the plugin will send audio back to Dorico. 

Each channel will have a name, displayed at bottom of the strip and editable by double clicking on the name. There will be a button to delete the strip. There will be a button to add a new strip. At the top of the strip there will be a selector to select the input for that strip, which must be a stream of Notes. For now, the only streams of Notes are the Notes that appear in the Time Line (which are created from incoming MIDI events). 

Just above the name, there will be a selector to select the plugin to use for that strip. 

For now, lets just ignore the audio output from the plugins. We will deal with that in the next step. You are correct that the audio return path is a major project.

We want one plugin per strip. The already loaded instances are just for testing. 

Is this clear? Do you have any questions?

## Step 4

The next step is to hook up the Notes (which are created from incoming MIDI events) to the plugins. 

## Step 4

The next step is to hook up the Notes (which are created from incoming MIDI events) to the plugins. We want to send Note-on and Note-off events to the plugins based on the Notes that are received on that channel. We will later send other MIDI events to the plugins to control their parameters. For now, we will just send Note-on and Note-off events to the plugins. 

We also want to establish a fixed (user-specified) delay for the plugin. If the user specified a 1 second delay, we want to delay the Note-on and Note-off events by 1 second before sending them to the plugin. This will allow future processing on the Notes. 