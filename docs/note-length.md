
In Dorico, the "played duration" of a note is a dynamic calculation. It starts with the **notated length** and layers several modifiers—articulations, slurs, and expression map rules—before arriving at the final MIDI note-off command.

## 1. Core Playback Durations
By default, Dorico leaves "air" between notes to ensure they don't sound mechanical or physically impossible (e.g., a wind player never breathing).

| Category | Item | Played Duration | Internal Logic |
| :--- | :--- | :--- | :--- |
| **Basic** | **Normal (Default)** | **95%** | Standard notes have a 5% gap for natural separation. |
| **Articulation** | **Staccato** | **50%** | Shortens the note by half for distinct separation. |
| | **Staccatissimo** | **25%** | An extremely short, pointed duration. |
| | **Tenuto** | **100%** | Played to the full length with 0% gap. |
| | **Marcato** | **50% – 75%** | Shorter than normal, usually coupled with a velocity boost. |
| **Slurs** | **Slurred Note** | **105%** | Overlaps the next note slightly to trigger VST legato. |
| | **Last Slurred Note** | **95%** | Reverts to the default "Normal" separation. |
| **Dynamics** | **Accent** | **100%** | Primarily a velocity shift, but removes the default 5% gap. |

---

## 2. Expression Map "Note Length" Thresholds
Expression maps allow Dorico to trigger specific samples (e.g., *Spiccato* vs. *Staccato* vs. *Détaché*) based on the actual duration of the note in seconds, which scales with tempo.

| Condition | Threshold (at 120 BPM) | Absolute Duration (Seconds) |
| :--- | :--- | :--- |
| **Very Short** | $\leq$ Dotted 16th | $\le 0.1875\text{s}$ |
| **Short** | $\leq$ Dotted 8th | $\le 0.375\text{s}$ |
| **Medium** | $\leq$ Dotted Quarter | $\le 0.75\text{s}$ |
| **Long** | $\leq$ Dotted Half | $\le 1.5\text{s}$ |
| **Very Long** | $>$ Dotted Half | $> 1.5\text{s}$ |

---

## 3. Technical Overrides & Advanced Logic

### The "Last Quarter Note" Rule
For notes longer than a quarter note, Dorico applies duration percentages (like Staccato's 50%) **only to the final quarter note** of the total duration. This prevents a whole note staccato from being truncated into a half note; instead, it plays for 3.5 beats and releases sharply.

### Humanization
Under **Library > Playback Options > Timing**, Dorico introduces "micro-jitter" to avoid the "machine gun" effect:
* **Humanize start positions:** Randomizes the MIDI Note-On by a few ticks.
* **Humanize durations:** Randomly varies the Note-Off timing by a small percentage.

### Manual Offsets (The Properties Panel)
Every note can be manually "nudged" in the **Properties Panel** (bottom of the screen in Play Mode or Write Mode).
* **Units:** Measured in **ticks**.
* **Resolution:** $$480\text{ ticks} = 1\text{ quarter note}$$
* **Settings:** You can specify a custom **Playback start offset** or **Playback end offset** to override all global logic for a specific note.

---

### Resources
* **Playback Options:** `Ctrl+Shift+P` (Windows) or `Cmd+Shift+P` (Mac).
* **Expression Maps:** `Library > Expression Maps`.
