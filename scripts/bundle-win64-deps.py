#!/usr/bin/env python3
"""Copy the recursive MinGW DLL closure of one or more PE executables."""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess


DLL_PATTERN = re.compile(r"^\s*DLL Name:\s*(.+?)\s*$", re.MULTILINE)

WINDOWS_SYSTEM_DLLS = {
    "advapi32.dll",
    "bcrypt.dll",
    "cfgmgr32.dll",
    "comctl32.dll",
    "comdlg32.dll",
    "crypt32.dll",
    "d3d11.dll",
    "d3d12.dll",
    "dinput8.dll",
    "dsound.dll",
    "dwmapi.dll",
    "dxgi.dll",
    "gdi32.dll",
    "imm32.dll",
    "iphlpapi.dll",
    "kernel32.dll",
    "msvcrt.dll",
    "ntdll.dll",
    "ole32.dll",
    "oleaut32.dll",
    "pathcch.dll",
    "powrprof.dll",
    "psapi.dll",
    "rpcrt4.dll",
    "secur32.dll",
    "setupapi.dll",
    "shell32.dll",
    "shlwapi.dll",
    "synchronization.dll",
    "user32.dll",
    "userenv.dll",
    "uuid.dll",
    "version.dll",
    "winmm.dll",
    "ws2_32.dll",
}


def is_windows_system_dll(name: str) -> bool:
    key = name.lower()
    return (
        key in WINDOWS_SYSTEM_DLLS
        or key.startswith("api-ms-win-")
        or key.startswith("ext-ms-win-")
    )


def imported_dlls(objdump: str, path: pathlib.Path) -> list[str]:
    output = subprocess.check_output(
        [objdump, "-p", str(path)], text=True, errors="replace"
    )
    return DLL_PATTERN.findall(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--search", action="append", required=True)
    parser.add_argument("--dest", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("binary", nargs="+")
    args = parser.parse_args()

    search_dirs = [pathlib.Path(item).resolve() for item in args.search]
    destination = pathlib.Path(args.dest).resolve()
    destination.mkdir(parents=True, exist_ok=True)

    available: dict[str, pathlib.Path] = {}
    for directory in search_dirs:
        if not directory.is_dir():
            continue
        for candidate in directory.glob("*.dll"):
            available.setdefault(candidate.name.lower(), candidate)

    pending = [pathlib.Path(item).resolve() for item in args.binary]
    analyzed: set[pathlib.Path] = set()
    bundled: dict[str, pathlib.Path] = {}
    system: set[str] = set()
    missing: set[str] = set()

    while pending:
        current = pending.pop()
        if current in analyzed:
            continue
        analyzed.add(current)
        for name in imported_dlls(args.objdump, current):
            key = name.lower()
            dependency = available.get(key)
            if dependency is None:
                if is_windows_system_dll(name):
                    system.add(name)
                else:
                    missing.add(name)
                continue
            if key in bundled:
                continue
            bundled[key] = dependency
            pending.append(dependency.resolve())

    for key in sorted(bundled):
        source = bundled[key]
        target = destination / source.name
        if source.resolve() != target.resolve():
            shutil.copy2(source, target)

    report = pathlib.Path(args.report)
    with report.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("Bundled DLLs\n============\n")
        for key in sorted(bundled):
            stream.write(f"{bundled[key].name}\t{bundled[key]}\n")
        stream.write("\nWindows-provided imports\n========================\n")
        for name in sorted(system, key=str.lower):
            stream.write(f"{name}\n")
        stream.write("\nMissing non-system imports\n==========================\n")
        for name in sorted(missing, key=str.lower):
            stream.write(f"{name}\n")

    print(f"Bundled {len(bundled)} DLLs into {destination}")
    if missing:
        raise SystemExit(
            "Missing non-system DLLs: " + ", ".join(sorted(missing, key=str.lower))
        )


if __name__ == "__main__":
    main()
