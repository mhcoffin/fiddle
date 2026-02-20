### Note classification

Note classification is the process of determining the attributes of a note based on the playing techniques that are active. The playing techniques are encoded in MIDI using a set of controllers. 

The expression map has a few base switches (Natural, Staccatissimo,  Staccato, Tenuto, and Staccato+Tenuto). Each of these is mapped to a value of CC102. Dorico will set the value of CC102 to the value corresponding to the playing technique that is active, or to the default if no playing technique is active. The value of CC102 should be used to determine the value of the "Articulation" attribute of the note. The "Articulation" attribute should be displayed a string that is one of the following: "Natural", "Staccatissimo", "Staccato", "Tenuto", "Staccato+Tenuto". The internal representation of the "Articulation" can be a string, int, or enum, whichever seems most appropriate.

In addition to the base switches, there a larger number of add-on switches. These are divided into mutual exclusion groups (MEGs). Each MEG corresponds to a note attribute. Using the same name makes sense.

Each MEG is assigned a single CC. There is nothing explicit in the MEG that indicates which CC it is assigned to. However, the add-on switches in the MEG should all set the same CC. If this is not the case, we should flag an error and ask the user to fix it. The values of that CC are used to determine the active playing technique in the MEG. Dorico will set the value of that CC to the value corresponding to the playing technique that is active, or to the default if no playing technique is active. 

The pt.natural switch is special. It belongs to every MEG. If this is not the case, we should flag an error and ask the user to fix it. 

Each add-on switch should belong to one and only one mutual exclusion group (MEG). If this is not the case, we should flag an error and ask the user to fix it. Each add-on switch should have an on-event and an off-event, each of which is a change to value of the CC that corresponds to the MEG the add-on switch belongs to. Dorico will set the CC to the value corresponding to the add-on switch when the switch is activated. All the add-on switches in a MEG should set values of the same CC. If this is not the case, we should flag an error and ask the user to fix it. When a switch is deactivated, Dorico will set the CC to the default value of the MEG. This is frequently but not always 0. 

The pt.natural switch should set all the MEG CCs to their default values. It should not have an off-event. The default value of the MEG can be found by looking at the off-events of all the add-on switches in the MEG.

For now, you can simply fix the expression map if you find inconsistencies.

