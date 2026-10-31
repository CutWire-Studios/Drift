#!/usr/bin/env python3
"""Refresh resources/addons/default-catalog.json, the store catalog bundled for first launch.

Fetches the live catalog; with --local DIR compiles it from a drift-addons checkout instead.
"""
import argparse
import subprocess
import sys
import urllib.request
from pathlib import Path

URL = "https://drift-addons.cutwire.org/v1/catalog"
OUT = Path(__file__).resolve().parent.parent / "resources" / "addons" / "default-catalog.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--local", metavar="DIR", help="drift-addons checkout to compile from")
    args = parser.parse_args()

    OUT.parent.mkdir(parents=True, exist_ok=True)
    if args.local:
        subprocess.run([sys.executable, str(Path(args.local) / "packer" / "catalog.py"),
                        "--out", str(OUT)], check=True)
        return
    try:
        with urllib.request.urlopen(URL, timeout=30) as response:
            body = response.read()
    except OSError as error:
        sys.exit(f"Could not fetch {URL} ({error}); the catalog may not be published yet. "
                 "Use --local ~/Projects/drift-addons instead.")
    OUT.write_bytes(body)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
