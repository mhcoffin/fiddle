#!/usr/bin/env python3
"""Generate each score test's MusicXML and expected result from specs/, plus
generated/index.json listing the tests. --check is strictly read-only and
fails if a generated file is stale or missing.
"""
import argparse
import json
import sys
from pathlib import Path

import scoretest

HERE = Path(__file__).resolve().parent
SPECS = HERE / "specs"
GENERATED = HERE / "generated"


def artifacts():
    files, index = {}, []
    for path in sorted(SPECS.glob("*.json")):
        spec, data = scoretest.load_spec(path)
        expected = scoretest.expected(spec, data)
        files[f"{spec['id']}.musicxml"] = scoretest.musicxml(spec)
        files[f"{spec['id']}.expected.json"] = (json.dumps(expected, indent=2) + "\n").encode()
        index.append(dict(id=spec["id"], title=spec["title"], description=spec.get("description", ""),
                          musicxml=f"{spec['id']}.musicxml", expected=f"{spec['id']}.expected.json",
                          instruments=sorted({p["instrument"] for p in expected["parts"]}),
                          notes=len(expected["notes"]), length_ms=expected["length_ms"]))
    files["index.json"] = (json.dumps(dict(schema_version=1, tests=index), indent=2) + "\n").encode()
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        files = artifacts()
    except scoretest.SpecError as error:
        sys.exit(f"Spec error: {error}")
    if args.check:
        stale = [name for name, data in files.items()
                 if not (GENERATED / name).exists() or (GENERATED / name).read_bytes() != data]
        extra = [p.name for p in GENERATED.glob("*") if p.name not in files]
        if stale or extra:
            sys.exit(f"Stale: {stale}; not from any spec: {extra}. Run generate.py.")
    else:
        GENERATED.mkdir(exist_ok=True)
        for p in GENERATED.glob("*"):
            if p.name not in files:
                p.unlink()
        for name, data in files.items():
            (GENERATED / name).write_bytes(data)
    print(("Checked" if args.check else "Generated")
          + f" {len(files)} files for {(len(files) - 1) // 2} tests")


if __name__ == "__main__":
    main()
