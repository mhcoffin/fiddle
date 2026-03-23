# Background

Dorico has a system for mapping expression map names to VST3 parameter names. 
This is done through a system of "Expression Maps" which are XML files Each entry in an expression map specifies a set of playback techniques. It maps this set to a collection of keyswitches, CC events, and other parameters that are sent to the VST3 plugin when the technique is played. 

# The Problem

We want Fiddle to be able to read these expression maps and use them to generate the correct MIDI events to send the the VST for each note. Each Note data structure in Fiddle already has a set of playback techniques. We need to load the expression map for the instrument and then use the techniques to generate the correct MIDI events to send the the VST for each note.

The expression map is not always complete: we may find notes that have a set of techniques that is different than any single entry in the expression map. In this case we need to find the entry that is the "closest" to the set of techniques and use that to generate the correct MIDI events to send the the VST for each note. The definition of "closest" is not yet defined. We will need to come up with a way to define "closest" and implement it. An approximation is to find the entry that has the most techniques in common with the set of techniques. We have a short explanation from Daniel Spreadbury, the head of the Dorico team at Steinberg, about how Dorico interprets expression maps in the file "Interpreting Expression Maps.md". We want to follow that as closely as possible.

Each entry in an expression map can also have a note-length condition: the generated MIDI will depend on the length of note. We will need to handle this in our note processing. 

# The Solution

We need new data structure that represents the expression map, at least part that associates a set of techniques with a set of MIDI events. We will have an affordance on each strip to laod an expression map. This data structure will be used to generate the correct MIDI events to send the the VST for each note in that strip. These MIDI events will be stored in the Note data structure and they will be sent to the VST when the note is played. Some events will be sent before the note is played (keyswitches) and some will be sent with the note (CC events). We will need to handle this in our note processing.

## Step 1

Provide an affordance to load an expression map into a strip. This will be done through a new file entry in the strip. This will open a file dialog to select an expression map file. 

## Step 2

Create a data structure to represent the expression map and fill it in from the expression map file. 

## Step 3

Add MIDI events to the Note data structure based on the expression map and the techniques that apply to the note. 

## Step 4

Send the MIDI events to the VST when the note is played. 

# Design Decisions

- **File format**: Standard Dorico `.doricolib` XML files.
- **Technique matching**: Start simple (best overlap), not strictly following Dorico's 6-level hierarchy. We are not bound to replicate Dorico's behavior exactly.
- **Techniques on a Note**: The `dimensions` field in the protobuf `Note` message contains the active playback techniques.
- **Note-length conditions**: Deferred to a later phase.
- **Keyswitch timing**: How far before the note should keyswitches be sent? Two sources of information: (1) the expression map itself may specify a pre-send time; (2) there is a default value that needs to be investigated. (TODO: determine the default keyswitch lead time.)
