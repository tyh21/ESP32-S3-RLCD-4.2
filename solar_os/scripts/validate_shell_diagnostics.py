#!/usr/bin/env python3
"""Static coverage checks for shell and registered-app diagnostics."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHELL_APP = ROOT / "src/apps/solar_os_shell.c"
APP_REGISTRY = ROOT / "src/apps/solar_os_app_registry.c"


def main() -> int:
    shell_text = SHELL_APP.read_text(encoding="utf-8")
    table = shell_text.split("static const shell_command_t shell_builtin_commands[] = {", 1)[1]
    table = table.split("};", 1)[0]
    builtins = re.findall(r'^\s*\{"([a-z][a-z0-9-]*)",', table, re.MULTILINE)
    if len(builtins) != 86:
        raise SystemExit(f"expected 86 built-in command declarations, found {len(builtins)}")
    duplicates = sorted({name for name in builtins if builtins.count(name) > 1})
    if duplicates:
        raise SystemExit(f"duplicate built-in commands: {', '.join(duplicates)}")

    registry_text = APP_REGISTRY.read_text(encoding="utf-8")
    entry_pattern = re.compile(
        r'APP_(?:FILE_)?ENTRY\("([^"]+)",\s*"[^"]+",\s*&[^,]+,.*?,\s*'
        r'"([^"]+)",\s*(\d+),\s*(\d+)(?:,\s*"[^"]+")?\),'
    )
    entries = entry_pattern.findall(registry_text)
    if len(entries) != 37:
        raise SystemExit(f"expected 37 registered app launch schemas, found {len(entries)}")
    for name, usage, min_argc_text, max_argc_text in entries:
        min_argc = int(min_argc_text)
        max_argc = int(max_argc_text)
        if not usage.startswith(name):
            raise SystemExit(f"{name}: usage does not start with app name: {usage}")
        if min_argc < 1 or (max_argc != 0 and max_argc < min_argc):
            raise SystemExit(f"{name}: invalid argc bounds {min_argc}..{max_argc}")

    file_entry_pattern = re.compile(
        r'APP_FILE_ENTRY\("([^"]+)",.*?,\s*\d+,\s*\d+,\s*"([^"]+)"\),'
    )
    file_entries = dict(file_entry_pattern.findall(registry_text))
    for name, extensions in file_entries.items():
        values = extensions.split()
        if not values or any(not value.startswith(".") or value.lower() != value
                             for value in values):
            raise SystemExit(f"{name}: invalid file extensions: {extensions}")
    if file_entries.get("gameboy") != ".gb":
        raise SystemExit("gameboy: expected .gb file association")

    forbidden = []
    for path in sorted((ROOT / "src/shell").glob("*.c")) + [SHELL_APP]:
        if path.name == "solar_os_shell_common.c":
            continue
        if "esp_err_to_name(" in path.read_text(encoding="utf-8"):
            forbidden.append(str(path.relative_to(ROOT)))
    if forbidden:
        raise SystemExit("raw esp_err_to_name remains in: " + ", ".join(forbidden))

    print(
        f"shell diagnostics coverage: {len(builtins)} built-ins, "
        f"{len(entries)} app schemas, {len(file_entries)} file associations"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
