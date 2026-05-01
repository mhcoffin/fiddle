#!/usr/bin/env python3
"""
train_hmm.py – Build HMM transition matrix from When-in-Rome corpus data.

Parses RomanText analysis.txt files (the standardized format used by the
"When in Rome" meta-corpus), extracts (key, chord) state transitions, and
outputs sparse binary assets for the C++ HarmonicAnalyzer.

Usage:
  # Clone the When-in-Rome corpus first:
  git clone https://github.com/MarkGotham/When-in-Rome.git /path/to/When-in-Rome

  # Train on ship-safe data only (TAVERN + Bach Chorales):
  python3 train_hmm.py --corpus /path/to/When-in-Rome --ship

  # Train on all data (including BPS-FH, for dev evaluation):
  python3 train_hmm.py --corpus /path/to/When-in-Rome --dev

  # Output goes to resources/hmm/ by default.

Binary Format (cpe_transitions.bin):
  Header:
    uint32  num_states  (expected 2592)
  Per state (num_states blocks):
    uint16  K           (number of outgoing transitions, ≤ 50)
    K × (uint16 target_state_idx, float32 log_prob)

The C++ loader (HarmonicAnalyzer::loadTransitionMatrix) reads this format.
"""

import argparse
import math
import os
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

# ─────────────────────────────────────────────────────────────────────────────
# Constants matching C++ HarmonicState.h
# ─────────────────────────────────────────────────────────────────────────────

NUM_QUALITIES = 9
NUM_ROOTS = 12
NUM_CHORDS = NUM_ROOTS * NUM_QUALITIES  # 108
NUM_KEYS = 24  # 12 major + 12 minor
NUM_STATES = NUM_KEYS * NUM_CHORDS      # 2592

TOP_K = 50          # Max outgoing transitions per state
LAPLACE_ALPHA = 1   # Laplace smoothing pseudocount

# Chord quality names matching C++ ChordQuality enum order
QUALITY_NAMES = [
    "Major", "Minor", "Diminished", "Augmented",
    "Dom7", "Maj7", "Min7", "Dim7", "HalfDim7"
]

# ─────────────────────────────────────────────────────────────────────────────
# Pitch class maps
# ─────────────────────────────────────────────────────────────────────────────

# Note name → pitch class (0-11, C=0)
NOTE_PC = {
    'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11,
    'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11,
}

def parse_key_string(key_str):
    """Parse a key string like 'C', 'f', 'Ab', 'f#' → (pitch_class, is_minor).

    Convention: uppercase = major, lowercase = minor.
    Accidentals: 'b'=flat, '#'=sharp (after the letter).
    """
    s = key_str.strip().rstrip(':').strip()
    if not s:
        return None

    letter = s[0]
    is_minor = letter.islower()
    pc = NOTE_PC.get(letter.upper())
    if pc is None:
        return None

    # Count accidentals
    rest = s[1:]
    for ch in rest:
        if ch == '#':
            pc = (pc + 1) % 12
        elif ch == 'b':
            pc = (pc - 1) % 12
        else:
            break  # ignore trailing characters

    return (pc, is_minor)


def key_index(root, is_minor):
    """Key root (0-11) + mode → key index (0-23). Matches C++ keyIndex()."""
    return root * 2 + (1 if is_minor else 0)


def chord_index(root, quality_idx):
    """Chord root (0-11) + quality (0-8) → chord index (0-107)."""
    return root * NUM_QUALITIES + quality_idx


def state_index(key_idx, chord_idx):
    """(keyIdx, chordIdx) → flat state index (0-2591)."""
    return key_idx * NUM_CHORDS + chord_idx


# ─────────────────────────────────────────────────────────────────────────────
# Roman Numeral Parser
#
# Parses numerals like: I, ii, V7, viio65, IV6, #ivo, bVII, V65/V, Ger65, etc.
# Returns (root_pc_relative_to_key, quality_idx) or None if unrecognized.
# ─────────────────────────────────────────────────────────────────────────────

# Scale degrees for major and minor keys (semitones above tonic)
MAJOR_DEGREES = [0, 2, 4, 5, 7, 9, 11]  # I II III IV V VI VII
MINOR_DEGREES = [0, 2, 3, 5, 7, 8, 10]  # i ii III iv v VI VII (natural minor)

# Roman numeral → scale degree (0-indexed)
NUMERAL_DEGREE = {
    'I': 0, 'II': 1, 'III': 2, 'IV': 3, 'V': 4, 'VI': 5, 'VII': 6,
    'i': 0, 'ii': 1, 'iii': 2, 'iv': 3, 'v': 4, 'vi': 5, 'vii': 6,
}

# Augmented sixth shorthands
AUG6_CHORDS = {
    'It': 'It6',
    'It6': 'It6',
    'Fr': 'Fr43',
    'Fr43': 'Fr43',
    'Fr65': 'Fr65',
    'Ger': 'Ger65',
    'Ger65': 'Ger65',
}

# Regex: optional accidental, Roman numeral, optional quality suffix, optional figures
ROMAN_RE = re.compile(
    r'^'
    r'(?P<acc>[#b]*)'           # accidentals on the root
    r'(?P<numeral>(?:[Ii]{1,3}|[Ii][Vv]|[Vv][Ii]{0,2}|[Vv]))'  # I-VII, i-vii
    r'(?P<quality>[o+ø]*)'      # o=dim, +=aug, ø=half-dim
    r'(?P<figures>\d*(?:/\d+)*(?:\[\w+\])*)'  # figured bass: 6, 65, 43, 42, 7, etc.
    r'(?:/(?P<secondary>[#b]*(?:[Ii]{1,3}|[Ii][Vv]|[Vv][Ii]{0,2}|[Vv])))?'  # secondary: /V, /iv
    r'$'
)


def parse_roman_numeral(rn_str, key_pc, is_minor):
    """Parse a Roman numeral string relative to a key.

    Returns (absolute_chord_root_pc, quality_idx) or None.
    """
    rn = rn_str.strip()
    if not rn or rn.startswith('Note:') or rn.startswith('Form:'):
        return None

    # Handle Cad64 as I
    if rn.startswith('Cad64') or rn.startswith('Cad'):
        return (key_pc, 0)  # Major triad on tonic

    # Handle augmented sixths
    for prefix, aug6_type in AUG6_CHORDS.items():
        if rn.startswith(prefix):
            # Italian/French/German 6ths are built on ♭6
            b6_pc = (key_pc + (8 if is_minor else 8)) % 12  # ♭6 from tonic
            if aug6_type.startswith('It'):
                return (b6_pc, 0)   # Approximate as major triad
            elif aug6_type.startswith('Fr'):
                return (b6_pc, 0)   # Approximate as major triad
            elif aug6_type.startswith('Ger'):
                return (b6_pc, 4)   # Approximate as dom7
            return None

    # Handle Neapolitan (N or N6)
    if rn.startswith('N') or rn == 'bII':
        bII_pc = (key_pc + 1) % 12
        return (bII_pc, 0)  # Major triad on ♭II

    # Standard Roman numeral parse
    m = ROMAN_RE.match(rn)
    if not m:
        return None

    acc = m.group('acc')
    numeral = m.group('numeral')
    quality_mark = m.group('quality')
    figures = m.group('figures')
    secondary = m.group('secondary')

    # Normalize numeral to get degree
    numeral_upper = numeral.upper()
    if numeral_upper not in NUMERAL_DEGREE:
        return None
    degree = NUMERAL_DEGREE[numeral_upper]

    # Get base pitch class from scale
    degrees = MINOR_DEGREES if is_minor else MAJOR_DEGREES
    base_pc = (key_pc + degrees[degree]) % 12

    # Apply accidentals
    for ch in acc:
        if ch == '#':
            base_pc = (base_pc + 1) % 12
        elif ch == 'b':
            base_pc = (base_pc - 1) % 12

    # Handle secondary dominants/leading tones (e.g., V/V, viio/V)
    # For secondaries, we resolve the target key and re-parse
    if secondary:
        sec_upper = secondary.upper().lstrip('#b')
        if sec_upper in NUMERAL_DEGREE:
            sec_degree = NUMERAL_DEGREE[sec_upper]
            target_pc = (key_pc + degrees[sec_degree]) % 12
            # The secondary function is relative to target_pc (treated as major)
            sec_degrees = MAJOR_DEGREES  # secondary targets are always major-ish
            sec_base_pc = base_pc  # already computed
            # Actually re-derive: the numeral is relative to the secondary target
            base_pc = (target_pc + MAJOR_DEGREES[degree]) % 12
            for ch in acc:
                if ch == '#':
                    base_pc = (base_pc + 1) % 12
                elif ch == 'b':
                    base_pc = (base_pc - 1) % 12

    # Determine quality
    is_upper = numeral[0].isupper()  # uppercase = major triad base

    # Clean figures (remove brackets, slashes)
    clean_fig = figures.replace('/', '').replace('[', '').replace(']', '')
    has_seventh = any(c in clean_fig for c in ['7', '65', '43', '42']) or \
                  '7' in figures

    # Check for figured bass 7th indicators
    if any(fig in figures for fig in ['7', '65', '43', '42']):
        has_seventh = True

    if quality_mark == 'o' or quality_mark == 'O':
        if has_seventh:
            return (base_pc, 7)   # Dim7
        return (base_pc, 2)       # Diminished triad
    elif quality_mark == 'ø' or quality_mark == '/o':
        return (base_pc, 8)       # HalfDim7
    elif quality_mark == '+':
        return (base_pc, 3)       # Augmented
    elif is_upper:
        if has_seventh:
            # Uppercase + 7th: could be Dom7 or Maj7
            # V7 = Dom7, I7/IV7 = Maj7 (simplification: treat V as Dom7, rest as Maj7)
            if degree == 4 or secondary:  # V= dominant
                return (base_pc, 4)   # Dom7
            return (base_pc, 5)       # Maj7
        return (base_pc, 0)           # Major triad
    else:
        if has_seventh:
            return (base_pc, 6)       # Min7
        return (base_pc, 1)           # Minor triad


# ─────────────────────────────────────────────────────────────────────────────
# RomanText File Parser
# ─────────────────────────────────────────────────────────────────────────────

def parse_analysis_file(filepath):
    """Parse a When-in-Rome analysis.txt file.

    Returns a list of (key_idx, chord_idx) state tuples representing the
    sequence of harmonic states in the piece.
    """
    states = []
    current_key_pc = 0
    current_key_minor = False
    key_set = False

    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()

            # Skip metadata, comments, empty lines
            if not line or line.startswith('Composer:') or line.startswith('Title:') \
               or line.startswith('Analyst:') or line.startswith('Proof-reader:') \
               or line.startswith('Time signature:') or line.startswith('Time Signature:') \
               or line.startswith('Note:') or line.startswith('Form:') \
               or line.startswith('Pedal:'):
                continue

            # Skip measure-repeat directives (m5-6 = m3-4)
            if '=' in line and line.startswith('m'):
                continue

            # Parse measure lines: "m1 C: I b2 IV b3 V7"
            if not line.startswith('m'):
                continue

            # Remove measure number prefix
            parts = line.split()
            if not parts:
                continue

            # Skip the measure number (m1, m0, etc.)
            chord_tokens = parts[1:]

            i = 0
            while i < len(chord_tokens):
                token = chord_tokens[i]

                # Beat marker (b1, b2, b1.5, etc.) — skip it, take next token
                if token.startswith('b') and len(token) > 1:
                    try:
                        float(token[1:])
                        i += 1
                        continue
                    except ValueError:
                        pass  # not a beat marker, treat as chord

                # Key change: "C:" or "f:" — key followed by colon
                if token.endswith(':'):
                    parsed_key = parse_key_string(token)
                    if parsed_key:
                        current_key_pc, current_key_minor = parsed_key
                        key_set = True
                    i += 1
                    continue

                # Pivot chord: || marker
                if token == '||':
                    i += 1
                    continue

                # Try to parse as Roman numeral
                if not key_set:
                    i += 1
                    continue

                result = parse_roman_numeral(token, current_key_pc, current_key_minor)
                if result:
                    chord_root_pc, quality_idx = result
                    k_idx = key_index(current_key_pc, current_key_minor)
                    c_idx = chord_index(chord_root_pc, quality_idx)
                    states.append((k_idx, c_idx))

                i += 1

    return states


# ─────────────────────────────────────────────────────────────────────────────
# Corpus Discovery
# ─────────────────────────────────────────────────────────────────────────────

# Ship-safe sub-corpora (CC BY-SA or public domain)
SHIP_SAFE_DIRS = [
    'Variations_and_Grounds',    # TAVERN (CC BY-SA 4.0)
    'Early_Choral',              # Bach Chorales + Monteverdi (public domain / permissive)
]

# Dev-only sub-corpora (GPL or NC licenses)
DEV_ONLY_DIRS = [
    'Piano_Sonatas',             # Includes BPS-FH (via When-in-Rome conversion)
    'Quartets',                  # Includes ABC corpus
    'Keyboard_Other',            # DCML romantic piano corpus
    'Textbooks',                 # Modulation examples
    'Songs',                     # Lieder etc.
]


def find_analysis_files(corpus_root, ship_only=True):
    """Find all analysis.txt files under the corpus.

    Args:
        corpus_root: Path to When-in-Rome repo root.
        ship_only: If True, only include ship-safe sub-corpora.
    """
    corpus_dir = Path(corpus_root) / 'Corpus'
    if not corpus_dir.exists():
        print(f"ERROR: Corpus directory not found: {corpus_dir}", file=sys.stderr)
        sys.exit(1)

    allowed_dirs = SHIP_SAFE_DIRS
    if not ship_only:
        allowed_dirs = SHIP_SAFE_DIRS + DEV_ONLY_DIRS

    files = []
    for subdir in allowed_dirs:
        search_dir = corpus_dir / subdir
        if search_dir.exists():
            for path in search_dir.rglob('analysis.txt'):
                files.append(path)
            # Also include analysis_B.txt (TAVERN second annotator)
            for path in search_dir.rglob('analysis_B.txt'):
                files.append(path)

    return sorted(files)


# ─────────────────────────────────────────────────────────────────────────────
# Transition Matrix Computation
# ─────────────────────────────────────────────────────────────────────────────

def build_transition_matrix(all_sequences, top_k=TOP_K, alpha=LAPLACE_ALPHA):
    """Build sparse top-K transition matrix with transpose-pooling.

    Transitions are normalized to relative scale degrees (e.g., I->ii7)
    so that C:Cmaj->C:Dmin7 and F:Fmaj->F:Gmin7 contribute to the same
    count. Pooled counts are replicated across all 12 key transpositions.

    This effectively multiplies training data by 12x and eliminates
    bias toward keys that are more common in the corpus.
    """
    # Phase 1: Count relative transitions
    rel_counts = defaultdict(lambda: defaultdict(int))
    total_transitions = 0

    for seq in all_sequences:
        for j in range(1, len(seq)):
            src_key_idx, src_chord_idx = seq[j - 1]
            dst_key_idx, dst_chord_idx = seq[j]

            src_key_root = src_key_idx // 2
            src_key_minor = src_key_idx % 2
            src_chord_root = src_chord_idx // NUM_QUALITIES
            src_chord_quality = src_chord_idx % NUM_QUALITIES

            dst_key_root = dst_key_idx // 2
            dst_key_minor = dst_key_idx % 2
            dst_chord_root = dst_chord_idx // NUM_QUALITIES
            dst_chord_quality = dst_chord_idx % NUM_QUALITIES

            # Normalize to relative degrees
            src_degree = (src_chord_root - src_key_root) % 12
            dst_degree = (dst_chord_root - dst_key_root) % 12
            key_interval = (dst_key_root - src_key_root) % 12

            rel_src = (src_key_minor, src_degree, src_chord_quality)
            rel_dst = (dst_key_minor, dst_degree, dst_chord_quality, key_interval)

            rel_counts[rel_src][rel_dst] += 1
            total_transitions += 1

    print(f"  Total transitions counted: {total_transitions}")
    print(f"  Unique relative source states: {len(rel_counts)}")

    # Phase 2: Replicate to all 12 key transpositions
    counts = defaultdict(lambda: defaultdict(int))

    for rel_src, dst_dict in rel_counts.items():
        src_minor, src_degree, src_quality = rel_src

        for key_root in range(12):
            abs_src_key = key_index(key_root, bool(src_minor))
            abs_src_chord_root = (key_root + src_degree) % 12
            abs_src_chord = chord_index(abs_src_chord_root, src_quality)
            abs_src = state_index(abs_src_key, abs_src_chord)

            for rel_dst, count in dst_dict.items():
                dst_minor, dst_degree, dst_quality, ki = rel_dst

                abs_dst_key_root = (key_root + ki) % 12
                abs_dst_key = key_index(abs_dst_key_root, bool(dst_minor))
                abs_dst_chord_root = (abs_dst_key_root + dst_degree) % 12
                abs_dst_chord = chord_index(abs_dst_chord_root, dst_quality)
                abs_dst = state_index(abs_dst_key, abs_dst_chord)

                counts[abs_src][abs_dst] += count

    print(f"  Unique absolute source states: {len(counts)}")
    print(f"  Amplification: {len(counts) / max(1, len(rel_counts)):.1f}x")

    # Phase 3: Build sparse matrix with Laplace smoothing
    sparse_matrix = [[] for _ in range(NUM_STATES)]

    for src in range(NUM_STATES):
        if src not in counts:
            sparse_matrix[src] = [(src, 0.0)]
            continue

        src_counts = counts[src]
        num_observed = len(src_counts)
        total = sum(src_counts.values()) + alpha * num_observed

        entries = []
        for dst, count in src_counts.items():
            log_prob = math.log((count + alpha) / total)
            entries.append((dst, log_prob))

        if src not in src_counts:
            entries.append((src, math.log(alpha / total)))

        entries.sort(key=lambda x: x[1], reverse=True)
        sparse_matrix[src] = entries[:top_k]

    return sparse_matrix


# ─────────────────────────────────────────────────────────────────────────────
# Binary Output
# ─────────────────────────────────────────────────────────────────────────────

def write_transitions_binary(sparse_matrix, output_path):
    """Write sparse transition matrix in the format expected by C++ loader.

    Format:
      uint32  num_states
      Per state:
        uint16  K  (number of outgoing transitions)
        K × (uint16 target_state_idx, float32 log_prob)
    """
    with open(output_path, 'wb') as f:
        # Header
        f.write(struct.pack('<I', NUM_STATES))

        total_entries = 0
        for src in range(NUM_STATES):
            entries = sparse_matrix[src]
            k = len(entries)
            f.write(struct.pack('<H', k))
            for dst, log_prob in entries:
                f.write(struct.pack('<Hf', dst, log_prob))
            total_entries += k

    file_size = os.path.getsize(output_path)
    print(f"  Wrote {output_path}")
    print(f"    States: {NUM_STATES}, Total entries: {total_entries}")
    print(f"    File size: {file_size:,} bytes ({file_size/1024:.1f} KB)")


# ─────────────────────────────────────────────────────────────────────────────
# Statistics
# ─────────────────────────────────────────────────────────────────────────────

PC_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def print_statistics(all_sequences, sparse_matrix):
    """Print summary statistics about the training data and matrix."""
    # Count key distribution
    key_counts = defaultdict(int)
    chord_counts = defaultdict(int)

    for seq in all_sequences:
        for k_idx, c_idx in seq:
            key_root = k_idx // 2
            key_minor = (k_idx % 2) == 1
            chord_root = c_idx // NUM_QUALITIES
            chord_quality = c_idx % NUM_QUALITIES

            key_name = PC_NAMES[key_root] + ('m' if key_minor else '')
            key_counts[key_name] += 1

            chord_name = PC_NAMES[chord_root] + ' ' + QUALITY_NAMES[chord_quality]
            chord_counts[chord_name] += 1

    print("\n── Key Distribution (top 15) ──")
    for key, count in sorted(key_counts.items(), key=lambda x: -x[1])[:15]:
        print(f"  {key:6s} {count:6d}")

    print("\n── Chord Distribution (top 15) ──")
    for chord, count in sorted(chord_counts.items(), key=lambda x: -x[1])[:15]:
        print(f"  {chord:18s} {count:6d}")

    # Matrix density
    non_empty = sum(1 for row in sparse_matrix if len(row) > 1)
    avg_k = sum(len(row) for row in sparse_matrix) / NUM_STATES
    print(f"\n── Matrix Statistics ──")
    print(f"  States with >1 transition: {non_empty} / {NUM_STATES}")
    print(f"  Average transitions per state: {avg_k:.1f}")

    # Most common transition
    all_transitions = []
    for src in range(NUM_STATES):
        for dst, lp in sparse_matrix[src]:
            all_transitions.append((src, dst, lp))

    all_transitions.sort(key=lambda x: -x[2])
    print("\n── Top 10 Transitions (by log-prob) ──")
    for src, dst, lp in all_transitions[:10]:
        src_key = src // NUM_CHORDS
        src_chord = src % NUM_CHORDS
        dst_key = dst // NUM_CHORDS
        dst_chord = dst % NUM_CHORDS

        src_k_root = src_key // 2
        src_k_minor = (src_key % 2) == 1
        src_c_root = src_chord // NUM_QUALITIES
        src_c_qual = src_chord % NUM_QUALITIES

        dst_k_root = dst_key // 2
        dst_k_minor = (dst_key % 2) == 1
        dst_c_root = dst_chord // NUM_QUALITIES
        dst_c_qual = dst_chord % NUM_QUALITIES

        src_name = f"{PC_NAMES[src_k_root]}{'m' if src_k_minor else ''}:{PC_NAMES[src_c_root]} {QUALITY_NAMES[src_c_qual]}"
        dst_name = f"{PC_NAMES[dst_k_root]}{'m' if dst_k_minor else ''}:{PC_NAMES[dst_c_root]} {QUALITY_NAMES[dst_c_qual]}"
        print(f"  {src_name:30s} → {dst_name:30s}  log_p={lp:.3f}")


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='Train HMM transition matrix from When-in-Rome corpus')
    parser.add_argument('--corpus', type=str, required=True,
                        help='Path to When-in-Rome repository root')
    parser.add_argument('--output', type=str, default=None,
                        help='Output directory (default: resources/hmm/)')
    parser.add_argument('--top-k', type=int, default=TOP_K,
                        help=f'Top-K transitions per state (default: {TOP_K})')
    parser.add_argument('--alpha', type=float, default=LAPLACE_ALPHA,
                        help=f'Laplace smoothing alpha (default: {LAPLACE_ALPHA})')

    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--ship', action='store_true',
                      help='Ship-safe mode: TAVERN + Bach Chorales only')
    mode.add_argument('--dev', action='store_true',
                      help='Dev mode: all corpora (including BPS-FH, ABC)')

    args = parser.parse_args()

    # Determine output directory
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent
    output_dir = Path(args.output) if args.output else project_root / 'resources' / 'hmm'
    output_dir.mkdir(parents=True, exist_ok=True)

    ship_only = args.ship

    print(f"╔══════════════════════════════════════════╗")
    print(f"║   HMM Training Pipeline                  ║")
    print(f"╚══════════════════════════════════════════╝")
    print(f"  Mode: {'SHIP (TAVERN + Bach)' if ship_only else 'DEV (all corpora)'}")
    print(f"  Corpus: {args.corpus}")
    print(f"  Output: {output_dir}")
    print(f"  Top-K: {args.top_k}, Alpha: {args.alpha}")
    print()

    # Find analysis files
    print("── Finding analysis files ──")
    files = find_analysis_files(args.corpus, ship_only=ship_only)
    print(f"  Found {len(files)} analysis files")

    if not files:
        print("ERROR: No analysis files found!", file=sys.stderr)
        sys.exit(1)

    # Parse all files
    print("\n── Parsing analyses ──")
    all_sequences = []
    parse_errors = 0
    total_states = 0

    for filepath in files:
        try:
            seq = parse_analysis_file(filepath)
            if len(seq) >= 2:  # Need at least 2 states for a transition
                all_sequences.append(seq)
                total_states += len(seq)
        except Exception as e:
            parse_errors += 1
            if parse_errors <= 5:
                print(f"  WARNING: Failed to parse {filepath}: {e}", file=sys.stderr)

    print(f"  Parsed {len(all_sequences)} pieces ({total_states} total states)")
    if parse_errors:
        print(f"  Parse errors: {parse_errors}")

    # Build transition matrix
    print("\n── Building transition matrix ──")
    sparse_matrix = build_transition_matrix(
        all_sequences, top_k=args.top_k, alpha=args.alpha
    )

    # Write binary output
    print("\n── Writing binary output ──")
    output_file = output_dir / 'cpe_transitions.bin'
    write_transitions_binary(sparse_matrix, output_file)

    # Print statistics
    print_statistics(all_sequences, sparse_matrix)

    print("\n✓ Done!")


if __name__ == '__main__':
    main()
