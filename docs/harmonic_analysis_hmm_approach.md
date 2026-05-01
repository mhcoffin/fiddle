# Real-Time Harmonic Analysis: Joint Key and Chord Detection

## 1. Core Architecture: The Joint-State HMM
A highly robust approach to harmonic analysis is to use a Hidden Markov Model (HMM) where the hidden states are defined as pairs of **(Key Center, Chord)**. Because keys and chords are mutually disambiguating, this joint modeling captures the hierarchy of functional harmony.

* **State Space:** 24 keys (12 major, 12 minor) × 108 chord types (12 roots × 9 qualities) = 2592 states.
* **Chord Qualities (9):**
    * **Triads (4):** Major, minor, diminished, augmented.
    * **Seventh Chords (5):** Dominant 7th, major 7th, minor 7th, diminished 7th, half-diminished 7th.

  Dominant and diminished 7ths are essential — they are the primary drivers of functional resolution in Common Practice harmony (V7 → I, viio7 → I). Half-diminished 7ths (iiø7) are critical for minor-key pre-dominant function. Augmented 6th chords (It+6, Fr+6, Ger+6) are modeled as enharmonic respellings of existing types (e.g., Ger+6 = dominant 7th built on ♭VI).

* **Time Slicing:** For real-time symbolic processing, an **event-based** approach is most efficient. The HMM state is only evaluated upon receiving MIDI `note-on` or `note-off` events, rather than polling at a fixed buffer or grid rate.

## 2. Observation Model: Figured-Bass Decomposition
Operating purely in the symbolic MIDI domain eliminates the need for complex audio DSP. Rather than collapsing all sounding notes into a single 12-dimensional chroma vector, the observation is decomposed into two components that mirror figured-bass analysis:

* **Bass Pitch Class (1-of-12):** The lowest sounding pitch class across all active notes. This is the single strongest cue for chord identity and inversion — a C-major triad over E in the bass (first inversion, I6) has radically different voice-leading implications than the same triad in root position.
* **Upper Chroma Vector (12-dim):** A pitch-class profile of all notes above the bass, weighted by duration and velocity.

The emission probability becomes P(bass_pc, upper_chroma | C_t), where C_t is the chord state. This factored observation gives the model inversion sensitivity without expanding the hidden state space to include inversions explicitly. Chord templates encode expected bass notes for each inversion (e.g., a I chord expects bass on scale degree 1 in root position, 3 in first inversion, 5 in second inversion), and the model learns to associate each template with the appropriate figured-bass profile.

Passing tones and non-harmonic tones are naturally smoothed by the transition matrix's preference for harmonic stability.

## 3. Model Parameters

### Transition Matrix (Prior Probabilities)
The 2592 × 2592 transition matrix dictates the flow of functional harmony. To prevent overfitting and manage sparsity, transitions are constrained by music theory priors:
* **Intra-key Transitions:** High probability. Chord movements within a key are weighted by diatonic function (e.g., V7 → I is highly probable; IV → I and ii → V are common; III → IV is rare).
* **Inter-key Transitions (Modulations):** Low probability penalty. Modulations are modeled by assigning higher probabilities to transitions involving "pivot chords" that are diatonic in both the source and destination keys. Closely related keys (relative major/minor, dominant, subdominant) receive higher modulation probability than remote keys.

### Substitutable Transition Matrices
Transition matrices are stored as loadable assets, allowing different harmonic idioms to be supported without retraining the entire system:
* **Default: Common Practice Era (CPE).** Trained on the datasets listed in §6. Covers Bach through Brahms.
* **Future extensions:** Romantic/chromatic, early 20th-century, film scoring, jazz. Each idiom has sufficiently different harmonic grammar (e.g., jazz ii-V-I chains, Romantic-era chromatic mediants) that a single matrix cannot serve all styles well.

Users select the active transition matrix to match their working idiom. This allows immediate progress on CPE music while deferring other styles.

## 4. Real-Time Implementation: Beam-Pruned Truncated Viterbi
Standard Viterbi decoding requires the entire sequence (offline processing). For an online, low-latency algorithm, a **Fixed-Lag Smoothing** algorithm (Truncated Viterbi) is used with **beam pruning** to keep computation bounded.

### Truncated Viterbi
* **Lookahead Lag:** The final chord decision is delayed by up to N MIDI events (typically 3 to 5) to allow future context to disambiguate the present. The lag is **dual-bounded**: by event count (N) and by wall-clock time (must not exceed Fiddle's configured processing delay). Whichever limit is reached first triggers the decision. In sparse passages where events arrive slowly, the time bound forces an early commit — this may reduce analysis quality, but ensures harmonic context is always available before Lua plugins process their notes. Users who need higher-quality analysis in sparse textures can increase the processing delay.
* **Ring Buffer:** Viterbi path metrics and backpointers are stored in a ring buffer of size N. On each new MIDI event, the forward step is calculated, and backpointers are traced N steps backwards to yield the locked-in chord decision. This avoids heap allocations and provides deterministic memory usage.

### Beam Pruning
With 2592 states, the naïve O(|S|²) forward step costs ~6.7M multiplications per event. Beam pruning reduces this to O(B × |S|) where B is the beam width:

* **Beam Width: B = 64–128.** At any given moment in tonal music, probability mass is concentrated in a small neighborhood: typically 2–4 plausible keys × 5–10 plausible chords within each key ≈ 10–40 high-probability states. A beam of 64 provides comfortable margin; 128 is conservative. This reduces the per-event cost to ~170K–330K operations — easily real-time even in dense passages.
* **Pruning Strategy:** After each forward step, retain only the top-B states by probability. States that fall below a threshold ratio of the best state's probability (e.g., 1/1000) are also culled.
* **Correctness Bound:** Beam search is an approximation, but for structured tonal music the true state is almost always within the beam. Empirically, beams above ~50 show negligible accuracy loss versus exact Viterbi on CPE repertoire.

### Threading
The HMM must **not** run on the audio thread. It runs on a dedicated worker thread (or the message thread), receiving note events via a lock-free queue from the `NoteStreamTracker`. Results are published back via a lock-free mechanism.

## 5. Integration with Fiddle

### Input: Merged Note Stream
The HMM observes a **merged stream of all active notes across all input channels**. Since harmony is a global property of the texture, notes from all Dorico desks/staves are aggregated into a single chroma observation. (Note: there may be more mixer strips than input channels; the merge happens at the input/NoteStreamTracker level, before per-strip processing.)

### Output: Harmonic Context for Lua Plugins
The current harmonic state — `(key_center, chord_root, chord_quality, bass_pitch_class)` — is published to the Lua plugin environment as read-only context. This allows Lua plugins to make articulation, dynamics, and transformation decisions based on harmonic function. For example:
* A string plugin could apply different bow articulations on dominant-resolution boundaries.
* A dynamics plugin could shape crescendos toward cadential arrivals.
* A transposition plugin could respect the current key when selecting enharmonic spellings.

The harmonic context is updated asynchronously. Its lag is bounded by the lesser of 3–5 MIDI events or the configured processing delay, so it is guaranteed to be available by the time Lua plugins run. In dense passages this provides high-quality smoothed analysis; in sparse passages the time bound may force earlier, less-informed decisions. Lua plugins should treat it as "recent harmonic state" rather than sample-accurate.

## 6. Training Datasets (Common Practice Era)
To train the transition matrices for classical repertoire, the following symbolically annotated datasets are recommended:

* **BPS-FH (Beethoven Piano Sonatas with Functional Harmony):** The optimal starting point. Contains time-aligned note events, beats, and exhaustive chord/key annotations (including Roman numerals). 
    * *Repository:* [Tsung-Ping/functional-harmony](https://github.com/Tsung-Ping/functional-harmony)
* **The Bach Chorales (JSB Dataset):** Foundational dataset for HMM music research, offering dense, highly functional pivot-chord modulations.
* **TAVERN Dataset:** A large collection of Beethoven and Mozart variations, annotated for keys and chords.
* **ABC (Annotated Beethoven Corpus):** Harmonic analyses of Beethoven's string quartets.

## 7. Key Literature
* **Algorithm Foundation:** G. David Forney Jr. – *"The Viterbi Algorithm"* (Proceedings of the IEEE, 1973). Covers the concept of "path merging" and decision delay.
* **Probabilistic Framework:** Kevin P. Murphy – *Machine Learning: A Probabilistic Perspective* (2012). Chapter 17 details the formal equations for Fixed-Lag Smoothing.
* **Musical Application:** Christopher Raphael – *"A Probabilistic Expert System for Automatic Musical Accompaniment"* (1999). Demonstrates real-time HMM decoding for live MIDI/audio accompaniment.
* **Chord Recognition with Structured Bass:** Juan P. Bello & Jeremy Pickens – *"A Robust Mid-Level Representation for Harmonic Content"* (ISMIR, 2005). Early work on separating bass from upper chroma for chord identification.
