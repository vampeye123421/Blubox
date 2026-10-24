# Luau API catalog

Studio ships `assets/editor/luau_api_catalog.json` as a compact, normalized seed. The C++ `CompletionDatabase` indexes it on a worker thread at startup; its schema supports the full class, datatype, enum, global, and library reference, including inherited members and signatures.

Refresh the reference data from Roblox's official Engine API `llms.txt` with Python 3:

```powershell
python tools/catalog/parse_roblox_docs.py
python tools/catalog/normalize_catalog.py tools/catalog/roblox_docs_raw.json --output tools/catalog/roblox_api_catalog.json
python tools/catalog/validate_catalog.py tools/catalog/roblox_api_catalog.json
python tools/catalog/build_completion_index.py tools/catalog/roblox_api_catalog.json --output assets/editor/luau_api_catalog.json
```

The fetcher reads `https://create.roblox.com/docs/reference/engine/llms.txt`, limits downloads to official Engine reference pages, and keeps successful pages if individual requests fail. The normalizer merges class, datatype, enum, library, and global entries from those pages with the checked-in core catalog, preserving Blubox-specific metadata. `build_completion_index.py` writes compact JSON; CMake copies it next to Studio and the release staging script includes it in Studio installers.
