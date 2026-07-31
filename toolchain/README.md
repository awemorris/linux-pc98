# Legacy i386 toolchain sources

The maintained compiler and C library sources are Git submodules. Initialize
them after cloning this repository:

```sh
git submodule update --init --recursive
```

| Path | Repository | Upstream base | Current status |
|---|---|---|---|
| `gcc/` | `awemorris/gcc-i386` | GCC 14.3.0 | Known-working i386 compiler baseline; no functional source patch yet |
| `musl/` | `awemorris/musl-i386` | musl 1.2.6 | TAS internal locks and the static low-memory profile |
| `glibc/` | `awemorris/glibc-i386` | glibc 2.43 | Pristine research baseline; the genuine i386 port has not started |

qemu-pc98 is a separate top-level submodule at `../qemu-pc98/`.

## Patch inventory

`patchsets/` is the main-repository record of every submodule delta. It
contains exact `git format-patch` exports, upstream versions, source archive
hashes, commit IDs, patch order, limitations, and validation requirements.

The submodule history is authoritative. The exported patches deliberately
duplicate that history so a future maintainer or AI agent can understand and
reapply the delta without first reconstructing repository relationships.

## Buildroot integration

`build-i386-rootfs.sh` writes these entries to the Buildroot output
directory's `local.mk`:

```make
GCC_INITIAL_OVERRIDE_SRCDIR = /path/to/linux-pc98/toolchain/gcc
GCC_FINAL_OVERRIDE_SRCDIR = /path/to/linux-pc98/toolchain/gcc
MUSL_OVERRIDE_SRCDIR = /path/to/linux-pc98/toolchain/musl
```

Buildroot copies these sources into its build directory and skips the normal
GCC and musl download, extraction, and patch phases. glibc is not part of the
current static-musl root filesystem. Binutils remains an official release
tarball managed by Buildroot.

## Updating a component

1. Update the component repository's `upstream` branch with a pristine
   stable release snapshot.
2. Tag it `upstream-<version>`.
3. Rebase the legacy-i386 commits on that tag and validate the result.
4. Update the submodule gitlink in this repository.
5. Regenerate `patchsets/<component>/<version>/` with `git format-patch`.
6. Update `patchsets/README.md` and the component-specific README.
7. Run `./toolchain/validate-patchsets.sh` and require all tree comparisons
   to report `OK`.
8. Run the component build, instruction scan, and qemu-pc98 boot tests
   before committing the new gitlink.
