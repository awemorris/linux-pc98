#!/usr/bin/env python3
"""Find exact normalized blocks added by the audited candidate commits."""

from __future__ import annotations

import re
import subprocess
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument(
    "--audit-tree", type=Path,
    default=Path("/home/awe/work/linux-v6.12-hayao-audit"),
)
parser.add_argument(
    "--current-tree", type=Path,
    default=Path("/home/awe/linux-pc98/linux-7.1"),
)
args = parser.parse_args()

AUDIT = args.audit_tree
CURRENT = args.current_tree

CANDIDATES = {
    "e1c3f4247": "early PC-98 console",
    "7f25ed79d": "modern uPD8251 serial",
    "a28cc9290": "low-1-MiB workaround",
    "23a4b8c32": "modern GDC consw",
    "a51b4854a": "modern libata frontend",
    "7180536ff": "GDC hardware cursor",
    "a10442563": "CPUID-less microcode workaround",
}

ALLOWED = {
    "5d96a282a": "keyboard supplied by Awe Morris",
    "b716545d0": "keyboard recovery supplied by Awe Morris",
}


def normalize(line: str) -> str | None:
    value = " ".join(line.strip().split())
    if not value or value.startswith(("/*", "*", "//")):
        return None
    return value


def current_lines(path: str) -> tuple[list[str], list[int]]:
    source = CURRENT / path
    if not source.exists():
        return [], []
    values: list[str] = []
    numbers: list[int] = []
    for number, line in enumerate(source.read_text(errors="replace").splitlines(), 1):
        value = normalize(line)
        if value is not None:
            values.append(value)
            numbers.append(number)
    return values, numbers


def added_blocks(commit: str):
    text = subprocess.check_output(
        ["git", "-C", str(AUDIT), "show", "--format=", "--unified=0", commit],
        text=True,
        errors="replace",
    )
    path: str | None = None
    block: list[str] = []

    def emit():
        nonlocal block
        if path and block:
            yield path, block
        block = []

    for line in text.splitlines():
        if line.startswith("diff --git "):
            yield from emit()
            match = re.match(r"diff --git a/(.+?) b/(.+)", line)
            path = match.group(2) if match else None
        elif line.startswith("@@"):
            yield from emit()
        elif line.startswith("+") and not line.startswith("+++"):
            value = normalize(line[1:])
            if value is not None:
                block.append(value)
        elif line.startswith(("-", " ")):
            yield from emit()
    yield from emit()


def occurrences(haystack: list[str], needle: list[str]):
    if len(needle) < 3:
        return
    width = len(needle)
    for index in range(len(haystack) - width + 1):
        if haystack[index:index + width] == needle:
            yield index


def scan(commits: dict[str, str], heading: str) -> None:
    print(f"# {heading}")
    total = 0
    for commit, description in commits.items():
        print(f"## {commit} {description}")
        found = 0
        for path, block in added_blocks(commit):
            current, numbers = current_lines(path)
            for index in occurrences(current, block):
                found += len(block)
                total += len(block)
                print(
                    f"{path}:{numbers[index]}-{numbers[index + len(block) - 1]} "
                    f"({len(block)} lines) {block[0][:100]}"
                )
        if not found:
            print("no exact normalized block of 3+ added code lines")
    print(f"TOTAL_MATCHED_NORMALIZED_LINES={total}")


scan(CANDIDATES, "candidate implementation blocks")
scan(ALLOWED, "explicitly permitted keyboard blocks")
