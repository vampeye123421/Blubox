#!/usr/bin/env python3
"""Fetch the official Roblox docs markdown pages into a raw page bundle."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import re
import sys
import time
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import urljoin, urlparse
from urllib.request import Request, urlopen

INDEX_URL = "https://create.roblox.com/docs/reference/engine/llms.txt"
OFFICIAL_HOST = "create.roblox.com"
LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)|(?<!\()https://create\.roblox\.com/docs/[^\s)<>]+")
REFERENCE_PATH = re.compile(r"/docs/reference/engine/(?:classes|datatypes|enums|libraries|globals)/", re.IGNORECASE)


def fetch(url: str, timeout: float = 25.0) -> str:
    request = Request(url, headers={"User-Agent": "BluboxStudio-CatalogBuilder/1.0", "Accept": "text/markdown,text/plain,*/*"})
    with urlopen(request, timeout=timeout) as response:
        return response.read().decode("utf-8", errors="replace")


def page_urls(index: str) -> list[str]:
    found = set()
    for match in LINK.finditer(index):
        raw = match.group(1) or match.group(0)
        raw = raw.rstrip(".,;:")
        url = urljoin(INDEX_URL, raw)
        parsed = urlparse(url)
        if parsed.netloc.lower() != OFFICIAL_HOST or not REFERENCE_PATH.search(parsed.path):
            continue
        if not parsed.path.lower().endswith(".md"):
            url = parsed._replace(path=parsed.path.rstrip("/") + ".md").geturl()
        found.add(url)
    return sorted(found)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("tools/catalog/roblox_docs_raw.json"))
    parser.add_argument("--index-url", default=INDEX_URL)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--limit", type=int, default=0, help="optional small limit for manual debugging")
    args = parser.parse_args()
    try:
        index_text = fetch(args.index_url)
        urls = page_urls(index_text)
        if args.limit > 0:
            urls = urls[:args.limit]
        if not urls:
            raise RuntimeError("the official llms.txt did not contain Engine reference markdown links")
        print(f"Found {len(urls)} official Engine reference pages; downloading with {args.workers} workers.")
        pages = []
        failures = []

        def load(url: str) -> tuple[str, str]:
            last_error = None
            for attempt in range(3):
                try:
                    return url, fetch(url)
                except (HTTPError, URLError, TimeoutError) as error:
                    last_error = error
                    if attempt < 2:
                        time.sleep(0.4 * (attempt + 1))
            raise RuntimeError(f"{url}: {last_error}")

        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.workers, 16))) as pool:
            futures = {pool.submit(load, url): url for url in urls}
            for future in concurrent.futures.as_completed(futures):
                try:
                    url, markdown = future.result()
                    pages.append({"url": url, "markdown": markdown})
                except Exception as error:  # keep successful pages and report missing ones
                    failures.append(str(error))
        pages.sort(key=lambda page: page["url"])
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"source": args.index_url, "pages": pages}, ensure_ascii=False), encoding="utf-8")
        print(f"Wrote {len(pages)} pages to {args.output}; failed pages: {len(failures)}")
        for failure in failures[:20]:
            print(f"WARN: {failure}", file=sys.stderr)
        return 0 if pages else 1
    except Exception as error:
        print(f"Roblox docs fetch failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
