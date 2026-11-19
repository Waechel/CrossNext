#!/usr/bin/env python3
"""Fail closed unless the build uses the ESP-IDF the Metalio Plus bootloader needs.

The Plus bootloader is ESP-IDF release/v6.1 at 9a97f6c5. Apps built with another
IDF lock up in early startup and the Plus has no OTA rollback (RickyOS
scripts/verify_plus_release_idf.py), so such an image must never be published.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

EXPECTED = "9a97f6c54ec638111ce55cd36581b3c192f15207"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--idf", required=True, type=Path)
    ap.add_argument("--project-description", type=Path)
    args = ap.parse_args()
    idf = args.idf.resolve(strict=True)
    rev = subprocess.check_output(["git", "-C", str(idf), "rev-parse", "HEAD"], text=True).strip()
    if rev != EXPECTED:
        print(f"Wrong ESP-IDF {rev}; Metalio Plus requires {EXPECTED}", file=sys.stderr)
        return 1
    dirty = subprocess.check_output(
        ["git", "-C", str(idf), "status", "--porcelain", "--untracked-files=no"], text=True
    ).strip()
    if dirty:
        print("ESP-IDF checkout has local modifications", file=sys.stderr)
        return 1
    if args.project_description:
        desc = json.loads(args.project_description.read_text())
        if desc.get("git_revision") not in (EXPECTED, EXPECTED[:8]) and not str(
            desc.get("git_revision", "")
        ).startswith(EXPECTED[:8]):
            print(f"Build reports IDF {desc.get('git_revision')}", file=sys.stderr)
            return 1
        if desc.get("target") != "esp32s31":
            print(f"Build target is {desc.get('target')}, not esp32s31", file=sys.stderr)
            return 1
    print(f"ESP-IDF {EXPECTED} verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
