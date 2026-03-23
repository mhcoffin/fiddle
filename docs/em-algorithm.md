The expression map has a list of base switches and a list of add-on switches. Each base switch consist of one or more playback techniques. They are mutually exclusive. Each base switch maps to a set of MIDI signals. 

Add-on switches also consist of one or more playback techniques. They are not mutually exclusive. They add MIDI signals to the set provided by the base switch.



# The Algorithm

A Fiddle Note has a list of playback techniques. It also has a length. The following algorithm is used to determine the base switch and any add-on switches that should be used for a note.

The following algorithm attempts to reverse engineer the algorithm used by Dorico. The goal is to select a single base switch and a set of add-on switches for each note. Ideally, the combination of the base switch and add-on switches should exactly match the note's PTs. If not, we will do our best to approximate the note's PTs. Important: we never want to select a base switch or add-on switch that is not a subset of the note's PTs.

The MIDI signals associated with a note are the union of the MIDI signals associated with the base switch and the add-on switches. Since many notes will have the same set of PTs, we can cache the results of this algorithm. 

## Definition

A base switch or add-on switch is more "specific" than another if it specifies more PTs. We want to specific switches over general ones.

## Preprocessing the Expression Map

The goal of this step is to pre-process each expression map to create a set of mutual exclusion groups of PTs, each with a default PT.

- If the expression map contains manually supplied MEGs, start with those.
  - If pt.natural appears in a manually supplied MEG, it is the default.
  - Otherwise, the first entry in the MEG is the default.

- Otherwise, start with the following MEGS:

  - {pt.arco, pt.pizzicato, pt.colLegno, pt.spiccato}
  - {pt.open, pt.muted, pt.harmonicMute, pt.cupMute, pt.straightMute, etc.} 
  - {pt.vibrato, pt.senzaVibrato, pt.moltoVibrato } 
  - {pt.nonLegato,  pt.legato}
  - {pt.snaresOn, pt.snaresOff}
  - {pt.natural, pt.accented, pt.tenuto}

  The first entry in each MEG above is the default PT for that MEG.
  

## Processing a Note

The goal of this step is to determine the base switch and add-on switches for each note.

1. **Expand PTs with MEG defaults.** Make sure that each note includes one PT from each MEG. If it does not, add the default PT for that MEG to the note's PTs.

2. **Determine note length.** We use the delay buffer to decide how much duration information is available:

    - If the note completes (note-off received) within the delay window, we know its exact duration. Adjust the "played" length based on the note's articulation (there are default rules, which can be overridden in the expression map, for how much to shorten or lengthen the note). Then classify the played length using the Dorico length categories: very short, short, medium, long, very long.

    - If the note does not complete within the delay window, we cannot wait any longer — the note-on must be released. In this case, assume the note is **very long**.

3. **Classify the note's length.** Use the Dorico length categories: very short, short, medium, long, very long. This is based on the "played" length found in step 2.

4. **Pick a base switch:**

    - Find all base switches that are subsets of the note's PTs. A base switch that contains PTs not in the note is not a candidate. E.g., if the note has PTs {A, B, C} and the base switch has PT {A, B, C, D}, it is not a candidate.

    - If a base switch has a length condition, the note must satisfy the length condition to be a candidate.

    - Choose the most specific candidate (the one with the largest number of PTs).

    - If there are more than one such base switch, pick one arbitrarily.

    - If none of the base switches provide any PTs in the Note, fall back on the "pt.natural" base switch, which we assume exists.

5. **Pick add-on switches:**

    - Include add-on switches that provide PTs that are in the note's PTs but not in the base switch's PTs.

    - Prefer more specific add-on switches over less specific ones.

    - Try to cover as many of the remaining PTs as possible by adding add-on switches.

    - If there is a conflict between covering more PTs and preferring more specific add-on switches, we will prefer covering more PTs.

6. **Caching.** Since many notes will have the same set of PTs and length classification, we cache the results of steps 4–5. The cache key is the tuple (expanded PT set, length category).


