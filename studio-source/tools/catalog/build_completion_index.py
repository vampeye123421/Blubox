#!/usr/bin/env python3
"""Validate a full catalog and write the compact JSON consumed by Studio."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from validate_catalog import validate


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--output", type=Path, default=Path("assets/editor/luau_api_catalog.json"))
    args = parser.parse_args()
    try:
        data = json.loads(args.catalog.read_text(encoding="utf-8"))
        counts = validate(args.catalog)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
        print(f"Indexed {counts['members']} members; wrote {args.output}")
        print(" " .join(f"{name}={counts[name]}" for name in ("classes", "dataTypes", "enums", "globals", "libraries")))
        return 0
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"index build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
