#!/usr/bin/env python3
"""Generate the pitched-percussion bridge map, allocation and PT inventory.

No installation or playback mutation. --check is strictly read-only.
"""
import argparse
from pathlib import Path

from profiles import PITCHED_PERCUSSION, engine

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    destination = HERE / f"pitched-percussion-r{PITCHED_PERCUSSION.REVISION}"
    outputs = engine.build(PITCHED_PERCUSSION)
    if not args.check:
        destination.mkdir(exist_ok=True)
    for name, content in outputs.items():
        path = destination / name
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                raise SystemExit(f"Stale/missing generated artifact: {path}")
        else:
            path.write_bytes(content)
    print(("Checked" if args.check else "Generated") + f" pitched percussion r1: {len(outputs)} artifacts")


if __name__ == "__main__":
    main()
