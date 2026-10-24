#!/usr/bin/env python3
"""Validate Blubox's normalized Luau catalog without third-party packages."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT_ARRAYS = ("globals", "libraries", "dataTypes", "enums", "classes")
MEMBER_ARRAYS = ("members", "functions", "constructors", "constants", "properties", "methods", "events", "callbacks", "items")


def validate(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schemaVersion") != 1:
        raise ValueError("expected an object with schemaVersion: 1")
    counts = {}
    for group in ROOT_ARRAYS:
        values = data.get(group)
        if not isinstance(values, list):
            raise ValueError(f"{group} must be an array")
        counts[group] = len(values)
        seen = set()
        for index, entity in enumerate(values):
            location = f"{group}[{index}]"
            if not isinstance(entity, dict) or not isinstance(entity.get("name"), str) or not entity["name"].strip():
                raise ValueError(f"{location} must be an object with a non-empty name")
            key = entity["name"].casefold()
            if key in seen:
                raise ValueError(f"duplicate {group} entry: {entity['name']}")
            seen.add(key)
            for member_group in MEMBER_ARRAYS:
                members = entity.get(member_group, [])
                if not isinstance(members, list):
                    raise ValueError(f"{location}.{member_group} must be an array")
                for member_index, member in enumerate(members):
                    if not isinstance(member, dict) or not isinstance(member.get("name"), str) or not member["name"].strip():
                        raise ValueError(f"{location}.{member_group}[{member_index}] requires a name")
    counts["members"] = sum(
        len(entity.get(group, []))
        for root in ROOT_ARRAYS
        for entity in data[root]
        for group in MEMBER_ARRAYS
    )
    return counts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("catalog", type=Path)
    args = parser.parse_args()
    try:
        counts = validate(args.catalog)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"catalog validation failed: {error}", file=sys.stderr)
        return 1
    for name in ROOT_ARRAYS:
        print(f"{name}: {counts[name]}")
    print(f"members: {counts['members']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
