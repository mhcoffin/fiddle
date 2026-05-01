#!/usr/bin/env python3
"""Inspect the binary transition matrix — show transitions from specific states."""

import struct
import sys
from pathlib import Path

NUM_QUALITIES = 9
NUM_CHORDS = 12 * NUM_QUALITIES
NUM_KEYS = 24
NUM_STATES = NUM_KEYS * NUM_CHORDS

PC_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
QUALITY_NAMES = ['maj', 'min', 'dim', 'aug', 'dom7', 'maj7', 'min7', 'dim7', 'hdim7']


def state_name(idx):
    key_idx = idx // NUM_CHORDS
    chord_idx = idx % NUM_CHORDS
    key_root = key_idx // 2
    key_minor = (key_idx % 2) == 1
    chord_root = chord_idx // NUM_QUALITIES
    chord_qual = chord_idx % NUM_QUALITIES
    return f"{PC_NAMES[key_root]}{'m' if key_minor else ''}:{PC_NAMES[chord_root]}{QUALITY_NAMES[chord_qual]}"


def load_transitions(path):
    data = Path(path).read_bytes()
    offset = 0
    num_states = struct.unpack_from('<I', data, offset)[0]
    offset += 4

    trans = {}
    for s in range(num_states):
        k = struct.unpack_from('<H', data, offset)[0]
        offset += 2
        entries = []
        for _ in range(k):
            target, logp = struct.unpack_from('<Hf', data, offset)
            offset += 6
            entries.append((target, logp))
        if entries:
            trans[s] = entries
    return trans


def key_index(root, minor):
    return root * 2 + (1 if minor else 0)


def chord_index(root, qual):
    return root * NUM_QUALITIES + qual


def state_index(key_idx, chord_idx):
    return key_idx * NUM_CHORDS + chord_idx


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'resources/hmm/cpe_transitions.bin'
    trans = load_transitions(path)

    # Show transitions from C major key + common chords
    test_states = [
        ("C:Cmaj",   key_index(0, False), chord_index(0, 0)),   # I in C
        ("C:Dmin",   key_index(0, False), chord_index(2, 1)),   # ii in C
        ("C:Dmin7",  key_index(0, False), chord_index(2, 6)),   # ii7 in C
        ("C:Gmaj",   key_index(0, False), chord_index(7, 0)),   # V in C
        ("C:Gdom7",  key_index(0, False), chord_index(7, 4)),   # V7 in C
        ("C:Fmaj",   key_index(0, False), chord_index(5, 0)),   # IV in C
        ("Am:Cmaj",  key_index(9, True),  chord_index(0, 0)),   # III in Am
        ("Am:Fmaj",  key_index(9, True),  chord_index(5, 0)),   # VI in Am
        ("Am:Fmaj7", key_index(9, True),  chord_index(5, 5)),   # VI7 in Am
    ]

    print(f"Loaded {len(trans)} states with transitions\n")

    for name, ki, ci in test_states:
        si = state_index(ki, ci)
        print(f"═══ {name} (state {si}) ═══")
        if si in trans:
            entries = sorted(trans[si], key=lambda x: -x[1])
            for target, logp in entries[:10]:
                print(f"  → {state_name(target):25s}  log_p={logp:+.3f}  (state {target})")
        else:
            print("  (no transitions)")
        print()

    # Also check: how many states in key C major have transitions?
    c_major_start = key_index(0, False) * NUM_CHORDS
    c_major_end = c_major_start + NUM_CHORDS
    c_covered = sum(1 for s in range(c_major_start, c_major_end) if s in trans)
    print(f"States with transitions in C major: {c_covered} / {NUM_CHORDS}")

    # Show all transitions that stay in C major
    print(f"\n═══ All transitions that STAY in C major ═══")
    count = 0
    for src in range(c_major_start, c_major_end):
        if src not in trans:
            continue
        for target, logp in trans[src]:
            if c_major_start <= target < c_major_end:
                print(f"  {state_name(src):25s} → {state_name(target):25s}  log_p={logp:+.3f}")
                count += 1
    print(f"Total in-key transitions: {count}")


if __name__ == '__main__':
    main()
