# Automatic Instrument Assignment in Dorico

This document contains what I’ve learned about how Dorico and NotePerformer work together to assign instruments to the plugin.

# Response from Daniel Spreadbury

I asked Daniel Spreadbury (Dorico head-honcho) how automatic instrument assignment works in NotePerformer and got the following reply:

> If you have a look at the NotePerformer playback template, which you’ll find in /Library/Application
> Support/Steinberg/Dorico 6 (or a folder named for an earlier version, if you have it installed; on my 
> system it’s actually in the Dorico 4 folder in that location), you’ll see roughly how it works.
>
> In PlaybackTemplateGenerators/NotePerformer, the playbacktemplategen.xml file specifies a type of 
> kMultiTimbralProgramChange. This tells Dorico that it can select patches by sending program changes,
> rather than loading patches directly in HALion.
>
> If you then look at PluginPresetLibraries/NotePerformer/presets.xml, you’ll see how each sound in NotePerformer
> is mapped to a combination of MSB, LSB, and program change, so that Dorico knows which program change message
> to issue to load a given sound.
> 
> Finally, the presets\_for\_instruments.xml file in the same folder specifies which specific presets
> (listed in presets.xml) Dorico should load for each instrument in the project.
> 
> This is all undocumented, I’m afraid, but hopefully this gives you an idea as to how it can be done.
> The crucial thing is that your plug-in needs to respond to program change messages received on each
> channel to load sounds there. Dorico will work its way sequentially through the MIDI channels for 
> each instrument in the score, like it does for NotePerformer.

# Manual Monitoring

In addition to asking Daniel, I monitored Dorico startup with 

```bash
    sudo fs\_usage \-w \-f pathname "Dorico 6" | grep NotePerformer
```

This reveals that Dorico does a stat64 call on the following files:

```text
        /Library/Application Support/Steinberg/Dorico 6/PluginPresetLibraries/NotePerformer
        /Library/Application Support/Steinberg/Dorico 5/PluginPresetLibraries/NotePerformer
        /Library/Application Support/Steinberg/Dorico 4/PluginPresetLibraries/NotePerformer
        /Library/Application Support/Steinberg/Dorico 3.5/PluginPresetLibraries/NotePerformer
        /Library/Application Support/Steinberg/Dorico 3/PluginPresetLibraries/NotePerformer
```

and then actually opens files under the Dorico 4 directory. This is probably because I installed NotePerformer when Dorico 4 was the latest version. Dorico then opens or tries to open the following files[^1]: 

```text
        /Library/Application Support/Steinberg/Dorico 4/PluginPresetLibraries/NotePerformer/presets.xml
        /Library/Application Support/Steinberg/Dorico 4/PluginPresetLibraries/NotePerformer/presets\_for\_instruments.xml
        /Library/Application Support/Steinberg/Dorico 4/PluginPresetLibraries/NotePerformer/expressionMapsDefinitions.xml
        /Library/Application Support/Steinberg/Dorico 4/PluginPresetLibraries/NotePerformer/drumKitNoteMaps.xml
        /Library/Application Support/Steinberg/Dorico 4/PlaybackTemplateGenerators/NotePerformer/playbacktemplategen.xml
        /Library/Application Support/Steinberg/Dorico 4/PlaybackTemplateGenerators/NotePerformer/playbacktemplatedeps.doricolib
```

The `expressionMapDefinitions.xml` and `drumKitNoteMaps.xml` don't actually exist. It appears that NotePerformer uses `playbacktemplatedeps.doricolib` to define the expression maps and drumkit maps.

# presets.xml

This file contains a series of entries of the form
```xml
<Preset>  
    <Name>Clarinet</Name>  
    <Category>Woodwinds</Category>  
    <ExpressionMap>xmap.user.noteperformer.default</ExpressionMap>  
    <Address>  
        <Program>1</Program>  
        <BankMSB\>0\</BankMSB\>  
        <BankLSB\>0\</BankLSB\>  
        <URI/\>  
    </Address\>  
</Preset\>
```

This is the specification for how to trigger the Clarinet preset in NotePerformer: set Program to 1 and bank to 0\.
There a many more entries like this. I don't know what <URI/> does.

# presets\_for\_instruments.xml

This contains entries of the following form:

```xml
<PresetsForInstrument\>  
    <Instrument\>instrument.wind.clarinet.aflat.alias.piccolo-clarinet\</Instrument\>  
    <Presets groupSize="kSolo"\>\<Preset preferred="true"\>Clarinet\</Preset\>\</Presets\>  
</PresetsForInstrument\>  
<PresetsForInstrument\>  
    <Instrument\>instrument.wind.clarinet.alto.eflat\</Instrument\>  
    <Presets groupSize="kSolo"\>\<Preset preferred="true"\>Clarinet\</Preset\>\</Presets\>  
</PresetsForInstrument\>
```

It looks like the `<Instrument\>` is the Dorico instrument ID, and `<Presets\>` references the name of an
entry in `presets.xml`. I am note sure what `preferred="true"` does.

I think this means that if the user chooses `instrument.wind.clarinet.aflat.alias.piccolo-clarinet` in setup mode, Dorico tells NotePerformer to use “Clarinet”, which entails setting Program 1, bank 0. Since NotePerformer uses synthesis to produce woodwind sounds, it can use the same program for all clarinets.

Question: can you add instruments by simply giving them a name here?

# playbacktemplategen.xml

This file contains the following:

```xml
\<?xml version="1.0" encoding="utf-8"?\>  
\<PlaybackTemplateGenerator\>  
        \<fileVersion\>1.2\</fileVersion\>  
        \<specID\>NotePerformer\</specID\>  
        \<name\>NotePerformer\</name\>  
        \<type\>kMultiTimbralProgramChange\</type\>  
        \<singlePluginDefinition\>  
                \<pluginID\>56535457494F416E6F7465706572666F\</pluginID\>  
                \<pluginName\>NotePerformer\</pluginName\>  
                \<pluginStateFile/\>  
                \<pluginPresetLibraryID\>NotePerformer\</pluginPresetLibraryID\>  
                \<numChannels\>16\</numChannels\>  
                \<audioOutputs\>16\</audioOutputs\>  
                \<audioOutputPerMIDIChannel\>false\</audioOutputPerMIDIChannel\>  
                \<reverbLevel\>0\</reverbLevel\>  
        \</singlePluginDefinition\>  
\</PlaybackTemplateGenerator\>
```

I guess that this tells Dorico how to use the plugin. Daniel says above that `<type\>kMultiTimbralProgramChange\</type\>` indicates that Dorico is needs to look for presets.xml to set up instruments that use the plugin. I guess the `<specID\>` entry or maybe the `<name\>` entry indicate which presets.xml to use. 

# playbacktemplatedeps.doricolib

This file contains a single expression map and a single drum-kit map. I guess this is loaded when the plugin is loaded? Really not sure.

# Implications for Fiddle

Duplicate this file/directory structure, but replacing NotePerformer with Fiddle. One way to do this is to have a Fiddle configuration file that contains the information that Dorico needs. This is mainly the list of instruments supported, with some data about each. Then have a Fiddle command (button or menu) that creates the files that Dorico needs and writes them into place. Fiddle could also provide a structured editor to create and modify the Fiddle configuration --- change the instruments, etc. So the procedure would be first to add the instruments that Fiddle is to support, then push "Install" to 
install Fiddle in Dorico, then start Dorico to pick up the configuration. 

In Fiddle the set of instruments is not completely static (as it is in NotePerformer). But it probably changes seldom enough that we can get away with writing the new configuration files and then restarting Dorico. We should save existing configuration files before updating them.

[^1]:  It opens some of these multiple times, but that doesn’t seem important.

