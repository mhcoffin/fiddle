Role: Expert Svelte & CSS Developer
Context: I am building "Fiddle," a VST host. The UI is implemented in Svelte within a JUCE WebView.
Task: Refactor the Mixer UI to support Visual Grouping for instrument strips that share the same input (e.g., three "Violin I" strips using different libraries).

Requirements:

Data Structure: Modify the mixer data model so that strips with the same instrument (e.g. "Violin I") are grouped together. A group should have the instrument as the label (e.g., "Violin I") and an array of subStrips (e.g., "Pro", "Elite", "Duality"), one for each library.

Bridging Header: Create a "Group Header" component that spans across all strips in a group.

If a group has multiple strips, the header should be a single continuous horizontal bar with the group label centered.

If a strip is standalone, it should either have a simpler header or none at all to maintain vertical alignment.

Visual Style:

Use CSS Grid or Flexbox to ensure the bridging header perfectly aligns with the widths of the strips below it.

Ensure the individual strip labels (the library names) sit immediately below this new bridging header.

The design should remain minimalist and dark-themed, consistent with a professional DAW.



Role: Expert Svelte Developer & Audio UI Engineer
Context: I am developing "Fiddle," a Svelte-based VST host in a JUCE WebView. I have successfully implemented visual grouping for my mixer strips. Now, I need to implement a Master Group Fader for these grouped instruments (e.g., a group of three "Violin I" libraries).

Task: Create a "Master Group" component that manages the volume and balance of multiple sub-strips.

Functional Requirements:

The Master Fader: * Add a dedicated "Master" fader strip to the left of the group.

This fader should control the overall output gain of the entire group. It should be slightly more prominent than the individual library faders.

The Individual Library Faders:

These should control the relative balance between the libraries (e.g., more "Elite," less "Duality").

Sum-Locked Logic (Normalization):

Implement a "Lock Sum" toggle on the Master strip.

The Logic: When "Lock Sum" is enabled, if I increase the fader for one library, the other two libraries in that group should automatically decrease their gain proportionally so that the total combined volume (the sum) remains constant.

Svelte State Management:

Use Svelte stores or a parent GroupController component to synchronize the gains.

Ensure that moving the Master fader scales the individual library gains without changing their relative balance.

Visual Styling:

The Master fader should look distinct (e.g., a wider track, a different colored fader cap like white or gold, or a "M" label).


I want to change the operation of the master fader. 
- At all times, the master fader setting should be the sum of all the individual library fader settings. 
- Adjusting the master fader should change the gain of all the individual library faders by the same amount. For example, if there are three library faders, and the master fader is moved up by 3db, then all three library faders should be moved up by 1db each.
- When the lock is "off", adjusting any individual library fader should not affect the other library faders.  However, the master fader should still be the sum of all the individual library fader settings, so it will change to be the sum.
- When the lock is "on", the master fader should be locked.  Adjusting any individual library fader should not change the master fader. Instead, the other library faders should adjust to keep the sum constant.  For example, if there are three library faders, and the lock is "on", adjusting one library fader by +2db should cause the other two library faders to decrease by 1db each, keeping the sum constant.

- the fader settings are currently constrained to -120 to +6. I still want this for display purposes, but internally, the faders should be unconstrained. This matters because, if the lock is on, and one library fader is moved down to -120, the other faders top out at +6. This causes the master fader to drop when it should be fixed (because of the lock).  The solution is to allow the internal fader values to be unconstrained, but still display them in the range -120 to +6.