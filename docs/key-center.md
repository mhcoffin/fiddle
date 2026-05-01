# Specification: Real-Time Tonal Center Classifier for Fiddle

## 1. Context & Objective
Implement a C++ classifier within the **FiddleServer** VST host that detects the "tonal center" (musical key) in real time. The system must analyze incoming Note data to annotate each note with tonal metadata before it reaches the Lua transformation layer.

## 2. Mathematical Foundation: Temperley Profiles
The classifier should use the **Krumhansl-Schmuckler (K-S) algorithm** logic but specifically utilize **Temperley’s European Classical profiles**, which provide better differentiation for common-practice era music (e.g., Bach).

### Temperley Weighting Vectors (12 Pitch Classes)
| Scale Degree | Major Weights | Minor Weights |
| :--- | :--- | :--- |
| **Tonic** | 5.0 | 5.0 |
| **m2** | 2.0 | 2.0 |
| **M2** | 3.5 | 3.5 |
| **m3** | 2.0 | 4.5 |
| **M3** | 4.5 | 2.0 |
| **P4** | 4.0 | 4.0 |
| **tritone** | 2.0 | 2.0 |
| **P5** | 4.5 | 4.5 |
| **m6** | 2.0 | 3.5 |
| **M6** | 3.5 | 2.0 |
| **m7** | 1.5 | 1.5 |
| **M7** | 4.0 | 4.0 |

> **Requirement:** Implementation must use a `KeyProfile` interface (or abstract class) to allow swapping these weights for others (e.g., Noland/Bach-specific) in future iterations.

## 3. Real-Time "Future-Looking" Architecture
The host operates with a user-adjustable delay (default ~1s). Use this window for "Lookahead Analysis."

1.  **Incoming Queue:** Notes arrive from the DAW/Sequencer and are timestamped for future playback.
2.  **The Lookahead Window:** The classifier maintains a 12-bin histogram of pitch classes found in the "Future Queue."
3.  **Duration Weighting:** Notes must be weighted by their duration. A whole note contributes 4x as much "tonal evidence" as a quarter note.
4.  **Exponential Decay (Leaky Integrator):** To handle modulations, use an **Exponentially Weighted Moving Average (EWMA)**. As time progresses, older notes in the buffer should "fade" in their contribution to the current key correlation.

## 4. Technical Requirements

### C++ Core Components
* **`PitchClassHistogram`**: A 12-float array updated as notes enter/exit the lookahead window.
* **`PearsonCorrelation`**: A function to calculate the correlation between the current histogram and the 24 templates (12 major, 12 minor).
* **`HysteresisManager`**: Prevents "flickering" between keys. A new key must be the "winner" for a configurable duration or by a certain confidence margin before the `current_key` state updates.

### Lua Integration
The classifier must output a `TonalContext` object to the Lua Note Transformer.

```cpp
struct TonalContext {
    int current_key_root; // 0-11
    bool is_minor;
    float confidence;     // Pearson R value
    bool is_diatonic;     // Is the specific note currently being processed in-key?
    int scale_degree;     // 1-7 (if diatonic)
};
```

5. Implementation Strategy for the Assistant
Phase 1: Profile Interface. Define an abstract IKeyProfile class and the TemperleyProfile implementation.

Phase 2: The Histogram. Implement a SlidingWindowHistogram that handles the 1s lookahead. Ensure it accounts for "Voice Density" (multi-instrument notes arriving simultaneously).

Phase 3: Correlation Engine. Write the Pearson Correlation logic. Optimize for performance (the 24 templates are constant and should be pre-calculated/normalized).

Phase 4: Lua Binding. Map the results to the existing Lua environment so that the user can call if note.tonal_context.is_diatonic then....

Critical Constraints
Performance: The correlation must happen every time a note is analyzed, but the "Current Key" can be cached and updated at a lower frequency (e.g., every 50ms) to save cycles.

Thread Safety: The future buffer is being populated by the MIDI-in thread and read by the classifier; use lock-free structures or appropriate mutexes where necessary.