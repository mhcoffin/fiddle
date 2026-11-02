#!/usr/bin/env python3
"""Write unpitched-probe.musicxml: two snare drums, one per probe percussion
map, whose hits, rolls, dynamics and marks answer README.md's unknowns.
--check fails if the committed file is stale.

MusicXML carries the hits, the dynamics ladder, the hairpin, accent and
marcato, the tremolo rolls, an x notehead and a parenthesized notehead. The
README lists what to add in Dorico by hand (rim, side stick, dead stroke,
Rods, Snares off/on, and the buzz roll or ghost note if the import drops
them).
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "tests" / "score"))
import scoretest  # noqa: E402

OUT = HERE / "unpitched-probe.musicxml"
TEMPO = 100
# Bars: 1-2 dynamics ladder; 3 eighths (beat stress); 4 accent, marcato,
# ghost, plain; 5 roll under p<f; 6 buzz roll, roll (+rim by hand); 7 rim,
# side stick (by hand), x head, dead stroke (by hand); 8 Rods (by hand);
# 9 the same hits marked rim (by hand); 10 Snares off (by hand); 11 Snares on.
PART_A = ("!ppp x/q !pp x/q !p x/q !mp x/q | !mf x/q !f x/q !ff x/q !fff x/q | "
          "!mf x/e x/e x/e x/e x/e x/e x/e x/e | x/q+acc x/q+marc x/q+ghost x/q | "
          "!p < x/w+trem | ] !f x/h+buzz x/h+trem | x/q x/q x/q+xhead x/q | "
          "x/q x/q x/q x/q | x/q x/q x/q x/q | x/q x/q x/q x/q | x/q x/q x/q x/q")
# Map B (Natural only): the same rolls, bars 5-6.
PART_B = "!p r/w | r/w | r/w | r/w | < x/w+trem | ] !f x/h+buzz x/h+trem | r/w | r/w | r/w | r/w | r/w"
SPEC = dict(id="unpitched-probe", title="Unpitched percussion probe", tempo=TEMPO, time="4/4",
            parts=[dict(id="A", name="Snare Drum (map A)", instrument="snare", music=PART_A),
                   dict(id="B", name="Snare Drum (map B)", instrument="snare", music=PART_B)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = scoretest.musicxml(SPEC)
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f"{OUT.name} is stale; run make_probe_score.py")
    else:
        OUT.write_bytes(data)
    print(("Checked " if args.check else "Wrote ") + OUT.name)


if __name__ == "__main__":
    main()
