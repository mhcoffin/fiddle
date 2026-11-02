#!/usr/bin/env python3
"""Write one-voice-switch-order.musicxml: one violin voice whose note lengths
change while one playing technique (Flautando, entered by hand) continues,
so Dorico changes the base switch (CC102) under the add-on four times.
--check fails if the committed file is stale.
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "tests" / "score"))
import scoretest  # noqa: E402

OUT = HERE / "one-voice-switch-order.musicxml"
# Quarters select the Medium Natural base (CC102=2) and dotted halves the
# >Long one (CC102=4), as in the r2 captures. Flautando from bar 1 to bar 3;
# nat. at bar 4.
SPEC = dict(id="one-voice-switch-order", title="Switch order: one voice", tempo=80, time="4/4",
            parts=[dict(id="vln", instrument="violin",
                        music="!mf C5/q D5/q E5/q F5/q | G5/h. E5/q | C5/h. D5/q | C5/q r/q r/h")])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = scoretest.musicxml(SPEC)
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f"{OUT.name} is stale; run make_score.py")
    else:
        OUT.write_bytes(data)
    print(("Checked " if args.check else "Wrote ") + OUT.name)


if __name__ == "__main__":
    main()
