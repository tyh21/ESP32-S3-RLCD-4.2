#!/usr/bin/env python3
"""Reject eager mutable state in foreground app translation units."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


APP_DESCRIPTOR = re.compile(
    r"(?:const|static\s+const)\s+solar_os_app_t\s+\w+\s*=\s*\{"
)
EXCEPTION = re.compile(
    r'^\s*SOLAR_OS_APP_STATIC_SRAM_EXCEPTION\("[^"\n]+"\)\s*$'
)
STATIC_START = re.compile(r"^\s*static\b")
CONST_TOKEN = re.compile(r"\bconst\b")
FUNCTION_DECLARATION = re.compile(r"^[^=;{}]*\([^;{}]*\)\s*(?:;|\{)")

APP_HELPERS = (
    "solar_os_gameboy_audio.c",
    "solar_os_gameboy_presenter.c",
)


def foreground_sources(repository: Path) -> list[Path]:
    apps = repository / "src" / "apps"
    sources = [
        path
        for path in sorted((repository / "src").rglob("*.c"))
        if APP_DESCRIPTOR.search(path.read_text(encoding="utf-8"))
    ]
    for name in APP_HELPERS:
        path = apps / name
        if path.exists() and path not in sources:
            sources.append(path)
    return sorted(sources)


def _previous_code_line(lines: list[str], index: int) -> str:
    for previous in range(index - 1, -1, -1):
        text = lines[previous].strip()
        if text and not text.startswith("//"):
            return lines[previous]
    return ""


def _static_declaration(lines: list[str], start: int) -> tuple[str, int]:
    declaration = lines[start].strip()
    end = start
    while end + 1 < len(lines) and not any(
        token in declaration for token in (";", "{")
    ):
        end += 1
        declaration += " " + lines[end].strip()
    return declaration, end


def violations(path: Path) -> list[tuple[int, str]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    found: list[tuple[int, str]] = []
    index = 0
    while index < len(lines):
        if not STATIC_START.match(lines[index]):
            index += 1
            continue
        declaration, end = _static_declaration(lines, index)
        if CONST_TOKEN.search(declaration) or FUNCTION_DECLARATION.match(declaration):
            index = end + 1
            continue

        # A single pointer is the cold-state slot or a dynamically owned
        # resource handle. Arrays and concrete objects allocate eager storage.
        if "*" in declaration and "[" not in declaration:
            index = end + 1
            continue
        if not EXCEPTION.match(_previous_code_line(lines, index)):
            found.append((index + 1, declaration))
        index = end + 1
    return found


def check(repository: Path) -> list[str]:
    errors: list[str] = []
    for path in foreground_sources(repository):
        for line, declaration in violations(path):
            relative = path.relative_to(repository)
            errors.append(f"{relative}:{line}: eager mutable app state: {declaration}")
    return errors


def lifecycle_bypasses(repository: Path) -> list[str]:
    errors: list[str] = []
    allowed = {
        repository / "src" / "solar_os.c",
        repository / "src" / "solar_os_jobs.c",
    }
    callback = re.compile(r"->(?:start|stop)\s*\(")
    for path in sorted((repository / "src").rglob("*.c")):
        if path in allowed:
            continue
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            if callback.search(line):
                errors.append(
                    f"{path.relative_to(repository)}:{line_number}: "
                    "foreground callback bypasses solar_os_app_start/stop"
                )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "repository",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    args = parser.parse_args()
    repository = args.repository.resolve()
    errors = check(repository) + lifecycle_bypasses(repository)
    if errors:
        print("\n".join(errors))
        print(
            "Move app state behind a cold pointer, or add "
            "SOLAR_OS_APP_STATIC_SRAM_EXCEPTION with a specific reason.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
