Daniel Spreadbury is the head of the Dorico team at Steinberg. He was asked how Dorico interprets expression maps. His response is below.

# Response from Daniel Spreadbury (Dorico)

This is a complex area, and even though I’ve read through the code to provide you with this answer, I wouldn’t be confident that I have captured every possible subtlety.

Dorico builds up a timeline of the playback techniques required for a particular instrument (or even an individual voice, if independent voice playback is active). Playback techniques are usually given rise to by playing technique items in the music, but can also be produced by dynamics and articulations.

As it builds up the timeline, Dorico adds playback techniques as they arrive, and removes them as they end. Some techniques are instantaneous, and so are automatically removed at the end of the note with which they correspond; other techniques are persistent, in which case they continue until they are countermanded by a mutually exclusive technique or a general reset (such as “nat.” or “ord.”), unless the playing technique item that gives rise to them has an explicit duration, in which case they are automatically removed at the end of that duration.

The timeline allows Dorico to know the ideal combination of playback techniques at every given rhythmic position. Dorico then attempts to deliver the closest possible combination of playback techniques for each combination using the playback techniques accommodated by the base and add-on switches in the expression map, taking into consideration the mutual exclusion groups similarly defined in the expression map.

It makes a series of decisions, in descending order of preference, to determine the final combination:

It determines if a base switch in the expression map provides the desired combination;
If not, it determines if a combination of a base switch plus one or more add-on switches provides the desired combination;
If not, it determines how many of the playback techniques in the desired combination are provided by each of the base switches, and in the specific case that the desired combination contains three or more playback techniques, it will settle on any combination that provides two of the three at this point;
If not (e.g. because the desired combination is for only two playback techniques), it uses a crude (and currently hard-coded) mechanism to determine the relative priority of each playback technique in the combination; only a handful of playback techniques are currently explicitly considered, and playback techniques with a larger difference in sound have a higher priority than those with a smaller difference. Harmonic and pizzicato are the highest priority, followed by mute, with all other playback techniques having the same, lower fallback priority;
If not, then Dorico looks for an explicit fallback technique (again, from a currently hard-coded list of fallbacks);
If not, then Dorico finally falls back on the “natural” playing technique, i.e. it will play whatever it would play if no explicit playback techniques were present.
In the project you uploaded, because Dorico prioritises mute above flutter-tongue, when there is no mute+flutter-tongue combination defined in your expression map, Dorico will choose mute in preference to flutter-tongue (using the fourth of the six rules detailed above).

General guidelines for producing a workable expression map:

Define explicit combinations using base switches for the techniques that are achieved by using e.g. different key switches or different patches in your sound library.
Use add-on switches for those techniques that can be layered on top of a particular base sound, i.e. one key switch or one patch.
Extend the basic mutual exclusion groups that are provided by default to help Dorico understand that one sound cannot be produced at the same time as another.

# Translating Expression Maps to code

As Daniel says, Dorico determines the playback techniques that apply to each note by examining the time line. Our universal expression map (UEM) captures changes to the timeline and transmits them to the Fiddle server. The Fiddle server then determines the playback techniques that apply to each note. So each Note in the Fiddle server has a list of playback techniques that apply to it. This information is equivalent to what Dorico obtains by examining the time line.

Without Fiddle, Dorico would play back the notes using the an expression map that is specific to the VSTi that is being used. We want to do the same. We'd like to make the playback identical to what Dorico would produce without Fiddle. We want the user to load into the Fiddle server their VSTi plugin along with an expression map for that plugin. 

