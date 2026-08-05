#!/usr/bin/env python3
"""Generate the review patch and one provenance row for every diff hunk."""

from __future__ import annotations

import argparse
import difflib
import re
from pathlib import Path


MARKER = re.compile(r"PC-?98|PC9800|pc9800|NEC98|nec98")
ROOTS = ("arch/x86", "block", "drivers", "include")


def source_class(path: str) -> str:
    if path.endswith(("drivers/scsi/pc980155.c",
                      "drivers/scsi/pc980155.h",
                      "drivers/scsi/wd33c93.c")):
        return "HISTORICAL-2.6.7 + UPSTREAM-7.1 + PROJECT-NEW"
    if "input/keyboard" in path:
        return "BSD + EXPLICIT-AWE-PERMISSION + UPSTREAM-7.1"
    if "input/mouse" in path:
        return "HISTORICAL-2.6.7 + UPSTREAM-7.1 + PROJECT-NEW"
    if "tty/serial" in path or path.endswith("serial_core.h"):
        return "HISTORICAL-2.6.7 + UPSTREAM-7.1 + PROJECT-NEW"
    if "video/fbdev" in path:
        return "STRATOHAL-ZLIB + UPSTREAM-7.1 + PROJECT-NEW"
    if "video/console" in path or path.endswith("early_printk.c"):
        return "HISTORICAL-2.6.7 + SPEC + UPSTREAM-7.1 + PROJECT-NEW"
    if "net/ethernet/8390" in path:
        return "SPEC + UPSTREAM-7.1 + PROJECT-NEW"
    if "drivers/ata" in path or "drivers/block" in path:
        return "HISTORICAL-2.6.7 + ATA-SPEC + UPSTREAM-7.1 + PROJECT-NEW"
    if "block/partitions" in path:
        return "HISTORICAL-2.6.7 + UPSTREAM-7.1 + PROJECT-NEW"
    return "HISTORICAL-2.6.7 + UPSTREAM-7.1 + PROJECT-NEW"


def is_source(path: Path) -> bool:
    return path.suffix in {".c", ".h"} or path.name.startswith("Kconfig") \
        or path.name.startswith("Makefile")


def relevant_paths(current: Path) -> list[str]:
    result: set[str] = set()
    for root in ROOTS:
        base = current / root
        for path in base.rglob("*"):
            if not path.is_file() or not is_source(path):
                continue
            relative = path.relative_to(current).as_posix()
            if relative.endswith("ne2k-lgy98.c"):
                result.add(relative)
                continue
            try:
                text = path.read_text(errors="replace")
            except OSError:
                continue
            if MARKER.search(text):
                result.add(relative)
    return sorted(result)


def file_lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    return path.read_text(errors="replace").splitlines(keepends=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vanilla-tree", type=Path, required=True)
    parser.add_argument("--current-tree", type=Path, required=True)
    parser.add_argument("--patch", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    patch_lines: list[str] = []
    rows = ["path\thunk\tprovenance\n"]
    for relative in relevant_paths(args.current_tree):
        old_path = args.vanilla_tree / relative
        new_path = args.current_tree / relative
        old = file_lines(old_path)
        new = file_lines(new_path)
        if old == new:
            continue
        old_label = f"a/{relative}" if old_path.exists() else "/dev/null"
        new_label = f"b/{relative}"
        body = list(difflib.unified_diff(old, new, old_label, new_label, n=3))
        patch_lines.append(f"diff --git a/{relative} b/{relative}\n")
        if not old_path.exists():
            patch_lines.append("new file mode 100644\n")
        patch_lines.extend(body)
        for line in body:
            if line.startswith("@@"):
                rows.append(
                    f"{relative}\t{line.strip()}\t{source_class(relative)}\n"
                )

    args.patch.write_text("".join(patch_lines))
    args.manifest.write_text("".join(rows))


if __name__ == "__main__":
    main()
