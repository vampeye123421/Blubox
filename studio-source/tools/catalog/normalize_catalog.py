#!/usr/bin/env python3
"""Normalize official Roblox Engine reference Markdown into the Blubox catalog."""

from __future__ import annotations

import argparse
import html
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse

GROUPS = ("globals", "libraries", "dataTypes", "enums", "classes")
SERVICE_NAMES = {
    "Workspace", "Players", "ReplicatedStorage", "ServerScriptService",
    "ServerStorage", "StarterPlayer", "StarterGui", "Lighting", "RunService",
    "ReplicatedFirst", "Teams", "SoundService", "TextChatService", "HttpService",
    "CollectionService", "TweenService", "UserInputService", "ContextActionService",
}
MEMBER_ARRAYS = {
    "members", "functions", "constructors", "constants", "properties",
    "methods", "events", "callbacks", "items",
}
LINK_RE = re.compile(r"\[([^\]]+)\]\([^)]*\)")
CODE_RE = re.compile(r"\x60([^\x60]*)\x60")
IDENTIFIER_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)")
SIGNATURE_RE = re.compile(r"\*\*Signature:\*\*\s*\x60([^\x60]+)\x60")
HEADING_RE = re.compile(r"^(#{2,4})\s+(.+?)\s*#*\s*$")


def clean(value: str) -> str:
    value = html.unescape(value.strip())
    value = LINK_RE.sub(r"\1", value)
    value = CODE_RE.sub(r"\1", value)
    value = re.sub(r"\*\*(.*?)\*\*|__(.*?)__", lambda match: match.group(1) or match.group(2), value)
    value = re.sub(r"!\[[^\]]*\]\([^)]*\)", "", value)
    return re.sub(r"\s+", " ", value).strip()


def front_matter(markdown: str) -> tuple[dict, str]:
    match = re.match(r"\A---\s*\n(.*?)\n---\s*(?:\n|$)", markdown, re.DOTALL)
    if not match:
        return {}, markdown
    metadata: dict[str, object] = {}
    current_list = ""
    for line in match.group(1).splitlines():
        list_item = re.match(r"^\s+-\s+(.+?)\s*$", line)
        if list_item and current_list:
            metadata.setdefault(current_list, []).append(list_item.group(1).strip(" \x22\x27"))
            continue
        pair = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):\s*(.*?)\s*$", line)
        if not pair:
            continue
        key, value = pair.groups()
        if value:
            metadata[key] = value.strip().strip("\x22\x27")
            current_list = ""
        else:
            metadata[key] = []
            current_list = key
    return metadata, markdown[match.end():]


def markdown_sections(body: str) -> list[tuple[str, str]]:
    lines = body.splitlines()
    sections: list[tuple[str, str]] = []
    section_name = ""
    section_lines: list[str] = []
    for line in lines:
        heading = HEADING_RE.match(line)
        if heading and len(heading.group(1)) == 2:
            if section_name:
                sections.append((clean(section_name), "\n".join(section_lines)))
            section_name = heading.group(2)
            section_lines = []
        else:
            section_lines.append(line)
    if section_name:
        sections.append((clean(section_name), "\n".join(section_lines)))
    return sections


def heading_blocks(text: str):
    lines = text.splitlines()
    heading = ""
    content: list[str] = []
    for line in lines:
        found = re.match(r"^###\s+(.+?)\s*#*\s*$", line)
        if found:
            if heading:
                yield clean(heading), "\n".join(content)
            heading = found.group(1)
            content = []
        elif not re.match(r"^####\s+", line):
            content.append(line)
    if heading:
        yield clean(heading), "\n".join(content)


def table_rows(text: str):
    lines = text.splitlines()
    for index, line in enumerate(lines[:-1]):
        if "|" not in line or "|" not in lines[index + 1]:
            continue
        separator = lines[index + 1].replace("|", "").replace(":", "").replace("-", "").replace(" ", "")
        if separator:
            continue
        for row in lines[index + 2:]:
            if "|" not in row:
                break
            cells = [cell.strip() for cell in row.strip().strip("|").split("|")]
            if cells:
                yield cells


def extract_documentation(text: str, *, property_member: bool = False) -> str:
    text = re.sub(r"\x60{3}.*?\x60{3}", "", text, flags=re.DOTALL)
    text = re.sub(r"^\s*\|.*$", "", text, flags=re.MULTILINE)
    text = re.sub(r"^\s*\*\*(?:Parameters|Returns|Yields|Tags|Thread safety|Security|History):.*$", "", text, flags=re.MULTILINE | re.IGNORECASE)
    text = SIGNATURE_RE.sub("", text)
    paragraphs = []
    for paragraph in re.split(r"\n\s*\n", text):
        paragraph = clean(re.sub(r"^\s*>\s?", "", paragraph, flags=re.MULTILINE))
        if not paragraph or paragraph.startswith("#") or paragraph.startswith("**Type:**"):
            continue
        if re.match(r"^(?:[-*]\s+|\|)", paragraph):
            continue
        if property_member and paragraph.startswith("{"):
            continue
        paragraphs.append(paragraph)
        if len(" ".join(paragraphs)) >= 600:
            break
    return " ".join(paragraphs)[:700]


def property_metadata(text: str) -> dict:
    for candidate in re.findall(r"\x60{3}(?:json)?\s*\n(.*?)\n\x60{3}", text, flags=re.DOTALL):
        try:
            parsed = json.loads(candidate)
        except json.JSONDecodeError:
            continue
        if isinstance(parsed, dict):
            return parsed
    return {}


def member_from_heading(heading: str, text: str, section: str, entity_kind: str) -> dict | None:
    explicit = re.match(r"^(Property|Method|Event|Callback|Function|Constructor|Constant):\s*(.+)$", heading, re.IGNORECASE)
    raw_name = explicit.group(2).strip() if explicit else heading.strip()
    section_lower = section.casefold()
    if explicit:
        kind = explicit.group(1).casefold()
    elif "constructor" in section_lower:
        kind = "constructor"
    elif "constant" in section_lower or entity_kind == "enum":
        kind = "constant"
    elif "propert" in section_lower:
        kind = "property"
    elif "event" in section_lower or "signal" in section_lower:
        kind = "event"
    elif "callback" in section_lower:
        kind = "callback"
    elif "function" in section_lower or "method" in section_lower:
        kind = "method"
    elif entity_kind == "library":
        kind = "method"
    else:
        return None

    raw_name = re.sub(r"\s+\*\((?:hidden|deprecated)[^)]*\)\*", "", raw_name, flags=re.IGNORECASE)
    raw_name = re.sub(r"\s+\((?:hidden|deprecated)[^)]*\)", "", raw_name, flags=re.IGNORECASE)
    raw_name = re.sub(r"\([^)]*\)$", "", raw_name)
    # The owner is the part before the final dot/colon in API member headings.
    member_token = re.split(r"[.:]", raw_name)[-1]
    found = IDENTIFIER_RE.search(member_token)
    if not found:
        return None
    name = found.group(1)
    if name.casefold() in {"properties", "methods", "events", "functions", "constructors", "constants"}:
        return None

    normalized_kind = {
        "function": "method",
        "constant": "constant",
    }.get(kind, kind)
    member: dict[str, object] = {"name": name, "kind": normalized_kind}
    signature = SIGNATURE_RE.search(text)
    if signature:
        member["signature"] = clean(signature.group(1))
    metadata = property_metadata(text) if normalized_kind == "property" else {}
    type_name = metadata.get("type")
    if isinstance(type_name, str) and type_name:
        member["type"] = type_name
    elif signature:
        returned = re.search(r"\)\s*:\s*([^,\s]+)", signature.group(1))
        if returned:
            member["type"] = returned.group(1).rstrip("?.")
    if normalized_kind == "constructor" or (entity_kind == "datatype" and "." in raw_name and ":" not in raw_name):
        member["isStatic"] = True
    if metadata.get("access") == "ReadOnly":
        member["readOnly"] = True
    if re.search(r"\bdeprecated\b", heading, re.IGNORECASE) or re.search(r"\bdeprecated\b", text[:400], re.IGNORECASE):
        member["deprecated"] = True
    documentation = extract_documentation(text, property_member=normalized_kind == "property")
    if documentation:
        member["documentation"] = documentation
    return member


def collect_members(body: str, entity_kind: str) -> dict[str, list[dict]]:
    result: dict[str, list[dict]] = {}
    for section, text in markdown_sections(body):
        if section.casefold() in {"description", "code samples", "inherited members", "math operations"}:
            continue
        if entity_kind == "enum" and section.casefold() == "items":
            items = []
            for cells in table_rows(text):
                raw = clean(cells[0]) if cells else ""
                token = re.split(r"[.:]", raw)[-1]
                match = IDENTIFIER_RE.search(token)
                if match and match.group(1).casefold() not in {"name", "value", "item"}:
                    item = {"name": match.group(1), "kind": "enumItem"}
                    if len(cells) > 2 and clean(cells[2]):
                        item["documentation"] = clean(cells[2])[:500]
                    items.append(item)
            if items:
                result["items"] = items
            continue
        if entity_kind == "datatype" and section.casefold() == "constants":
            constants = []
            for cells in table_rows(text):
                if not cells:
                    continue
                raw = clean(cells[0])
                token = re.split(r"[.:]", raw)[-1]
                match = IDENTIFIER_RE.search(token)
                if not match or match.group(1).casefold() in {"name", "constant"}:
                    continue
                constant = {"name": match.group(1), "kind": "constant", "isStatic": True}
                if len(cells) > 1:
                    constant["type"] = clean(cells[1])
                if len(cells) > 2 and clean(cells[2]):
                    constant["documentation"] = clean(cells[2])[:500]
                constants.append(constant)
            if constants:
                result["constants"] = constants
            continue

        for heading, member_text in heading_blocks(text):
            member = member_from_heading(heading, member_text, section, entity_kind)
            if not member:
                continue
            member_kind = member["kind"]
            array_name = {
                "property": "properties",
                "event": "events",
                "callback": "callbacks",
                "constructor": "constructors",
                "constant": "constants",
                "method": "functions" if entity_kind == "library" else "methods",
            }[member_kind]
            result.setdefault(array_name, []).append(member)
    for array_name, members in result.items():
        unique: dict[str, dict] = {}
        for member in members:
            unique[member["name"]] = member
        result[array_name] = list(unique.values())
    return result


def parse_page(page: dict) -> tuple[str, list[dict]] | None:
    url = page.get("url", "")
    path = urlparse(url).path
    markdown = page.get("markdown", "")
    patterns = (
        ("classes", "classes", "class"),
        ("datatypes", "dataTypes", "datatype"),
        ("enums", "enums", "enum"),
        ("libraries", "libraries", "library"),
        ("globals", "globals", "global"),
    )
    for path_segment, group, kind in patterns:
        marker = f"/reference/engine/{path_segment}/"
        if marker not in path:
            continue
        filename_name = unquote(path.split(marker, 1)[1].split("/", 1)[0].removesuffix(".md"))
        if not filename_name:
            return None
        metadata, body = front_matter(markdown)
        page_name = str(metadata.get("name") or filename_name)
        summary = clean(str(metadata.get("summary") or ""))
        source_url = url

        if group == "globals":
            entries = []
            for section, section_text in markdown_sections(body):
                if section.casefold() not in {"properties", "functions"}:
                    continue
                member_kind = "property" if section.casefold() == "properties" else "method"
                for heading, item_text in heading_blocks(section_text):
                    entry_name = re.split(r"[.:]", re.sub(r"\s+\*\(deprecated\)\*", "", heading, flags=re.IGNORECASE))[-1]
                    identifier = IDENTIFIER_RE.search(entry_name)
                    if not identifier:
                        continue
                    name = identifier.group(1)
                    type_name = ""
                    if member_kind == "property":
                        type_match = re.search(r"\*\*Type:\*\*\s*\x60([^\x60]+)\x60", item_text)
                        if type_match:
                            type_name = clean(type_match.group(1))
                    signature = SIGNATURE_RE.search(item_text)
                    entry: dict[str, object] = {
                        "name": name,
                        "kind": "global",
                        "sourceUrl": source_url,
                    }
                    if type_name:
                        entry["type"] = type_name
                    elif signature:
                        entry["type"] = name
                    if signature:
                        entry["signature"] = clean(signature.group(1))
                    if re.search(r"\bdeprecated\b", heading, re.IGNORECASE) or re.search(r"\bdeprecated\b", item_text[:400], re.IGNORECASE):
                        entry["deprecated"] = True
                    documentation = extract_documentation(item_text)
                    if documentation:
                        entry["documentation"] = documentation
                    entries.append(entry)
            return group, entries

        tags = metadata.get("tags", [])
        has_service_tag = isinstance(tags, list) and any(str(tag).casefold() == "service" for tag in tags)
        item_kind = "service" if group == "classes" and (page_name in SERVICE_NAMES or has_service_tag) else kind
        item: dict[str, object] = {
            "name": page_name,
            "kind": item_kind,
            "documentation": summary,
            "sourceUrl": source_url,
        }
        if group == "classes":
            inherits = metadata.get("inherits", [])
            if isinstance(inherits, list) and inherits:
                base = str(inherits[0]).strip()
                if base and base != page_name:
                    item["baseClass"] = base
        elif group == "dataTypes":
            item["type"] = page_name
        if not summary:
            description = next((text for name, text in markdown_sections(body) if name.casefold() == "description"), "")
            item["documentation"] = extract_documentation(description)
        item.update(collect_members(body, kind))
        return group, [item]
    return None


def merge_entity(existing: dict, update: dict) -> dict:
    for key, value in update.items():
        if key not in MEMBER_ARRAYS:
            if key == "kind" and value:
                existing[key] = value
                continue
            if value and not existing.get(key):
                existing[key] = value
            continue
        prior = {member.get("name", ""): member for member in existing.get(key, [])}
        for member in value:
            name = member.get("name", "")
            if name in prior:
                prior[name].update({field: field_value for field, field_value in member.items() if field_value and not prior[name].get(field)})
            else:
                prior[name] = member
        existing[key] = list(prior.values())
    return existing


def merge_group(base_entries: list[dict], updates: list[dict]) -> list[dict]:
    by_name = {entry["name"].casefold(): entry for entry in base_entries if entry.get("name")}
    for entry in updates:
        key = entry["name"].casefold()
        by_name[key] = merge_entity(by_name[key], entry) if key in by_name else entry
    return sorted(by_name.values(), key=lambda entry: entry["name"].casefold())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=Path, help="output from parse_roblox_docs.py")
    parser.add_argument("--base", type=Path, default=Path("assets/editor/luau_api_catalog.json"))
    parser.add_argument("--output", type=Path, default=Path("tools/catalog/roblox_api_catalog.json"))
    args = parser.parse_args()
    try:
        raw = json.loads(args.raw.read_text(encoding="utf-8"))
        catalog = json.loads(args.base.read_text(encoding="utf-8")) if args.base.exists() else {"schemaVersion": 1, **{group: [] for group in GROUPS}}
        catalog["schemaVersion"] = 1
        catalog["generatedAt"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
        catalog["source"] = raw.get("source", "https://create.roblox.com/docs/reference/engine/llms.txt")
        updates: dict[str, list[dict]] = {group: [] for group in GROUPS}
        for page in raw.get("pages", []):
            parsed = parse_page(page)
            if parsed is not None:
                group, entries = parsed
                updates[group].extend(entries)
        for group in GROUPS:
            catalog[group] = merge_group(catalog.setdefault(group, []), updates[group])
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"Normalized {sum(len(entries) for entries in updates.values())} official entries into {args.output}")
        return 0
    except (OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
        print(f"catalog normalization failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
