#!/usr/bin/env python3
"""Generate woodwind/brass inspection maps, allocations and full PT inventories.

No installation or playback mutation. --check is strictly read-only.
"""
import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "orchestral-strings-bridge"))

import generate as engine
from profiles import PROFILES


def artifacts(section):
    return engine.build(PROFILES[section])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--section", choices=("all", *PROFILES), default="all")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--dorico-catalog", type=Path)
    args = parser.parse_args()
    if args.dorico_catalog:
        engine.check_dorico(args.dorico_catalog)
    sections = PROFILES if args.section == "all" else (args.section,)
    for section in sections:
        outputs = artifacts(section)
        destination = HERE / f"{section}-r1"
        if not args.check:
            destination.mkdir(exist_ok=True)
        for name, content in outputs.items():
            path = destination / name
            if args.check:
                if not path.exists() or path.read_bytes() != content:
                    raise SystemExit(f"Stale/missing generated artifact: {path}")
            else:
                path.write_bytes(content)
        print(("Checked" if args.check else "Generated") + f" {section} r1: {len(outputs)} artifacts")


if __name__ == "__main__":
    main()
