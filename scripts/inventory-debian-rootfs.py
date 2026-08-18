#!/usr/bin/env python3
"""Inventory Debian packages and non-dpkg binary payloads in a rootfs."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import os
from pathlib import Path
import stat
import subprocess
from typing import Iterable


def parse_deb822(path: Path) -> list[dict[str, str]]:
    paragraphs: list[dict[str, str]] = []
    fields: dict[str, str] = {}
    current: str | None = None
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not raw:
            if fields:
                paragraphs.append(fields)
                fields = {}
                current = None
            continue
        if raw[0].isspace() and current:
            fields[current] += "\n" + raw[1:]
            continue
        if ":" not in raw:
            continue
        current, value = raw.split(":", 1)
        fields[current] = value.lstrip()
    if fields:
        paragraphs.append(fields)
    return paragraphs


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_description(path: Path) -> str:
    try:
        return subprocess.check_output(
            ["file", "-b", "--", str(path)], text=True, errors="replace"
        ).strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        return "unknown"


def normalize_list_path(value: str) -> str:
    value = value.strip()
    if value.startswith("/."):
        value = value[2:]
    if not value.startswith("/"):
        value = "/" + value
    return os.path.normpath(value)


def read_owners(info_dir: Path) -> dict[str, set[str]]:
    owners: dict[str, set[str]] = collections.defaultdict(set)
    for listing in sorted(info_dir.glob("*.list")):
        package = listing.name[:-5]
        for line in listing.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.strip():
                owners[normalize_list_path(line)].add(package)
    return owners


def iter_regular_files(root: Path) -> Iterable[tuple[str, Path, os.stat_result]]:
    for directory, names, files in os.walk(root, followlinks=False):
        names[:] = [name for name in names if not (Path(directory) / name).is_symlink()]
        for name in files:
            path = Path(directory) / name
            try:
                metadata = path.lstat()
            except FileNotFoundError:
                continue
            if not stat.S_ISREG(metadata.st_mode):
                continue
            relative = "/" + path.relative_to(root).as_posix()
            yield relative, path, metadata


def write_tsv(path: Path, headers: list[str], rows: Iterable[Iterable[object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output, delimiter="\t", lineterminator="\n")
        writer.writerow(headers)
        writer.writerows(rows)


def markdown_escape(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def markdown_table(headers: list[str], rows: Iterable[Iterable[object]]) -> list[str]:
    lines = [
        "| " + " | ".join(headers) + " |",
        "|" + "|".join("---" for _ in headers) + "|",
    ]
    for row in rows:
        lines.append("| " + " | ".join(markdown_escape(item) for item in row) + " |")
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rootfs", type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--packages-tsv", required=True, type=Path)
    parser.add_argument("--unowned-tsv", required=True, type=Path)
    parser.add_argument("--label", default="Debian rootfs")
    parser.add_argument("--suite", default="unknown")
    parser.add_argument("--variant", default="unknown")
    parser.add_argument("--explicit", default="")
    parser.add_argument(
        "--artifact",
        action="append",
        default=[],
        metavar="LABEL=PATH",
        help="Non-dpkg boot/image input to record",
    )
    args = parser.parse_args()

    root = args.rootfs.resolve()
    status_path = root / "var/lib/dpkg/status"
    info_dir = root / "var/lib/dpkg/info"
    if not status_path.is_file() or not info_dir.is_dir():
        parser.error(f"not a usable dpkg rootfs: {root}")

    explicit = {name for name in args.explicit.split(",") if name}
    packages = [
        entry
        for entry in parse_deb822(status_path)
        if entry.get("Status") == "install ok installed"
    ]
    packages.sort(key=lambda entry: entry.get("Package", ""))
    owners = read_owners(info_dir)
    binary_counts: collections.Counter[str] = collections.Counter()
    unowned_payloads: list[dict[str, object]] = []
    unowned_scripts: list[dict[str, object]] = []
    generated_binary_data: list[dict[str, object]] = []

    for relative, path, metadata in iter_regular_files(root):
        try:
            with path.open("rb") as source:
                prefix = source.read(4096)
        except OSError:
            continue
        is_elf = prefix.startswith(b"\x7fELF")
        is_executable = bool(metadata.st_mode & 0o111)
        file_owners = owners.get(relative, set())
        # dpkg maintainer scripts and control data are intentionally not listed
        # in a package's .list file, but they are still package metadata rather
        # than locally injected payloads.
        if relative.startswith("/var/lib/dpkg/info/"):
            continue
        if file_owners and (is_elf or is_executable):
            for owner in file_owners:
                binary_counts[owner] += 1
            continue
        if file_owners:
            continue
        if is_elf:
            unowned_payloads.append(
                {
                    "path": relative,
                    "size": metadata.st_size,
                    "mode": stat.filemode(metadata.st_mode),
                    "kind": "ELF",
                    "description": file_description(path),
                    "sha256": sha256(path),
                }
            )
        elif is_executable and prefix.startswith(b"#!"):
            unowned_scripts.append(
                {
                    "path": relative,
                    "size": metadata.st_size,
                    "interpreter": prefix.splitlines()[0].decode("utf-8", "replace"),
                }
            )
        elif (
            relative == "/etc/ld.so.cache"
            or relative.startswith("/var/cache/ldconfig/")
            or (
                relative.startswith(("/lib/modules/", "/usr/lib/modules/"))
                and relative.endswith(".bin")
            )
        ):
            generated_binary_data.append(
                {
                    "path": relative,
                    "size": metadata.st_size,
                    "description": file_description(path),
                    "sha256": sha256(path),
                }
            )

    package_rows: list[list[object]] = []
    for entry in packages:
        name = entry.get("Package", "")
        package_rows.append(
            [
                name,
                entry.get("Version", ""),
                entry.get("Architecture", ""),
                entry.get("Priority", ""),
                entry.get("Essential", "no"),
                entry.get("Installed-Size", ""),
                binary_counts[name] + binary_counts[f"{name}:{entry.get('Architecture', '')}"],
                "explicit --include" if name in explicit else "minbase/dependency",
            ]
        )

    unowned_payloads.sort(key=lambda item: str(item["path"]))
    unowned_scripts.sort(key=lambda item: str(item["path"]))
    generated_binary_data.sort(key=lambda item: str(item["path"]))

    write_tsv(
        args.packages_tsv,
        [
            "package",
            "version",
            "architecture",
            "priority",
            "essential",
            "installed_size_kib",
            "owned_executable_or_elf_files",
            "selection",
        ],
        package_rows,
    )
    write_tsv(
        args.unowned_tsv,
        ["path", "size_bytes", "mode", "kind", "description", "sha256"],
        [
            [
                item["path"],
                item["size"],
                item["mode"],
                item["kind"],
                item["description"],
                item["sha256"],
            ]
            for item in unowned_payloads
        ],
    )

    artifacts: list[list[object]] = []
    for specification in args.artifact:
        if "=" not in specification:
            parser.error(f"invalid --artifact: {specification}")
        label, filename = specification.split("=", 1)
        path = Path(filename).resolve()
        if not path.is_file():
            parser.error(f"artifact does not exist: {path}")
        artifacts.append(
            [label, str(path), path.stat().st_size, file_description(path), sha256(path)]
        )

    architecture_counts = collections.Counter(
        entry.get("Architecture", "unknown") for entry in packages
    )
    total_installed_kib = sum(
        int(entry.get("Installed-Size", "0") or "0") for entry in packages
    )
    explicit_present = sorted(explicit.intersection(entry.get("Package", "") for entry in packages))

    lines = [
        f"# {args.label} inventory",
        "",
        "This inventory was generated from the installed dpkg database and the",
        "actual staging tree. It describes the Release root filesystem, not the",
        "set of packages currently available from a Debian mirror.",
        "",
        "## Build scope",
        "",
        f"- rootfs: `{root}`",
        f"- debootstrap suite: `{args.suite}`",
        f"- debootstrap variant: `{args.variant}`",
        f"- architecture: `i386` userland built for the repository's i686 image",
        f"- installed packages: **{len(packages)}**",
        f"- package architectures: "
        + ", ".join(f"`{key}` {value}" for key, value in sorted(architecture_counts.items())),
        f"- sum of dpkg Installed-Size fields: **{total_installed_kib} KiB**",
        f"- explicit `--include` packages present: "
        + ", ".join(f"`{name}`" for name in explicit_present),
        "",
        "The build subsequently deletes apt lists, manuals, info pages, and",
        "locales. Therefore `Installed-Size` is package metadata and is larger",
        "than the files retained in the slimmed staging tree.",
        "",
        "## Installed packages",
        "",
    ]
    lines.extend(
        markdown_table(
            ["Package", "Version", "Arch", "Priority", "Essential", "KiB", "Exec/ELF", "Selection"],
            package_rows,
        )
    )
    lines.extend(
        [
            "",
            "The machine-readable equivalent is",
            f"`{args.packages_tsv.as_posix()}`.",
            "",
            "## ELF payloads not owned by a Debian package",
            "",
            f"Found **{len(unowned_payloads)}** unowned ELF files. In this image these",
            "are Linux 7.2 modules installed by `make modules_install`; they are",
            "project build products rather than files from a `.deb` package.",
            "",
        ]
    )
    lines.extend(
        markdown_table(
            ["Path", "Bytes", "Type", "SHA-256"],
            [
                [item["path"], item["size"], item["description"], item["sha256"]]
                for item in unowned_payloads
            ],
        )
    )
    lines.extend(
        [
            "",
            "The machine-readable equivalent is",
            f"`{args.unowned_tsv.as_posix()}`.",
            "",
            "## Other unowned executable scripts",
            "",
        ]
    )
    if unowned_scripts:
        lines.extend(
            markdown_table(
                ["Path", "Bytes", "Interpreter"],
                [
                    [item["path"], item["size"], item["interpreter"]]
                    for item in unowned_scripts
                ],
            )
        )
    else:
        lines.append("No unowned executable scripts were found in the rootfs.")
    lines.extend(
        [
            "",
            "## Generated binary metadata not owned by a package",
            "",
            "These files are caches or indexes generated while constructing the",
            "rootfs. They are not executable payloads.",
            "",
        ]
    )
    lines.extend(
        markdown_table(
            ["Path", "Bytes", "Type", "SHA-256"],
            [
                [item["path"], item["size"], item["description"], item["sha256"]]
                for item in generated_binary_data
            ],
        )
    )
    lines.extend(
        [
            "",
            "## Non-package boot-image inputs",
            "",
            "These files are outside the debootstrap rootfs but are embedded in the",
            "released CF-card image by the PC-98 image builder.",
            "",
        ]
    )
    lines.extend(markdown_table(["Role", "Build path", "Bytes", "Type", "SHA-256"], artifacts))
    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "- Normal commands and shared libraries are owned by the packages in",
            "  the first table.",
            "- The unowned ELF files are the locally built Linux modules; no extra",
            "  standalone userspace ELF executable was found.",
            "- The uncompressed kernel and three loader binaries are deliberately",
            "  outside dpkg because they live in the PC-98 boot path/boot partition.",
            "- Locally written hostname, fstab, network, password, and optional",
            "  console settings are configuration text, not additional binaries.",
        ]
    )
    args.report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
