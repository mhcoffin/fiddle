#!/usr/bin/env python3
"""Generate the unpitched-percussion bridge (expression map + percussion map
in one library), its allocation and PT inventory, and the Phase 0 probe.

No installation or playback mutation. --check is strictly read-only.
"""
import argparse
from pathlib import Path

from profiles import PROBE, REVISIONS, engine

HERE = Path(__file__).resolve().parent


def targets():
    yield PROBE, HERE / "probe"
    for spec in REVISIONS:
        yield spec, HERE / f"unpitched-percussion-r{spec.REVISION}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for spec, destination in targets():
        outputs = engine.build(spec)
        if not args.check:
            destination.mkdir(exist_ok=True)
        for name, content in outputs.items():
            path = destination / name
            if args.check:
                if not path.exists() or path.read_bytes() != content:
                    raise SystemExit(f"Stale/missing generated artifact: {path}")
            else:
                path.write_bytes(content)
        print(("Checked" if args.check else "Generated") + f" {destination.name}: {len(outputs)} artifacts")


if __name__ == "__main__":
    main()
