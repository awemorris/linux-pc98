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
| `glibc/` | `awemorris/glibc-i386` | glibc 2.41 | Validated genuine-i386 port at `cddb5b71c3ee05561cc78ed9c7f8f1e04f703d68` on `port/glibc-2.41-i386` |

qemu-pc98 is a separate top-level submodule at `../qemu-pc98/`.

## Patch inventory

`external/patchsets/` is the main-repository record of every submodule delta. It
contains exact `git format-patch` exports, upstream versions, source archive
hashes, commit IDs, patch order, limitations, and validation requirements.

The submodule history is authoritative. The exported patches deliberately
duplicate that history so a future maintainer or AI agent can understand and
reapply the delta without first reconstructing repository relationships.

## Buildroot integration

`scripts/build-i386-rootfs.sh` writes these entries to the Buildroot output
directory's `local.mk`:

```make
GCC_INITIAL_OVERRIDE_SRCDIR = /path/to/linux-pc98/external/gcc
GCC_FINAL_OVERRIDE_SRCDIR = /path/to/linux-pc98/external/gcc
MUSL_OVERRIDE_SRCDIR = /path/to/linux-pc98/external/musl
```

Buildroot copies these sources into its build directory and skips the normal
GCC and musl download, extraction, and patch phases. The release BusyBox
rootfs remains the bootstrap/static-musl environment. The glibc validation
scripts reuse its compiler and BusyBox applet configuration, then link a
separate dynamic BusyBox against glibc 2.41. Binutils remains an official
release tarball managed by Buildroot.

## glibc 2.41 genuine-i386 validation

The glibc port depends on the versioned atomic service in this repository's
Linux 7.2 tree. It is deliberately enabled only for an exact i386 build;
i486 uses the normal glibc code and native CPU atomics.

```sh
./build-glibc.sh i386
./build-glibc-tests.sh i386
./build-glibc-busybox.sh i386
./check-glibc-i386-opcodes.sh

./build-glibc.sh i486
./build-glibc-tests.sh i486
./build-glibc-busybox.sh i486
```

The current qemu-pc98 validation passed with both `-cpu 386` and `-cpu 486`.
On i386, dynamic loading, malloc, TLS, pthread mutexes, fork, process-shared
robust mutexes, COW/read-only fault containment, and the 12-test atomic UAPI
selftest all pass. A dynamically glibc-linked BusyBox also runs as
`/sbin/init`. See `patchsets/glibc/2.41/README.md` for the implemented design,
test matrix, and remaining Debian packaging gates.

The glibc implementation is recorded in submodule commit
`cddb5b71c3ee05561cc78ed9c7f8f1e04f703d68`. Its portable export is
`external/patchsets/glibc/2.41/0001-Add-Linux-assisted-atomics-for-genuine-i386.patch`.
`scripts/validate-patchsets.sh` replays that patch from `upstream-2.41` and requires
the resulting tree to match the pinned submodule revision exactly.

## Updating a component

1. Update the component repository's `upstream` branch with a pristine
   stable release snapshot.
2. Tag it `upstream-<version>`.
3. Rebase the legacy-i386 commits on that tag and validate the result.
4. Update the submodule gitlink in this repository.
5. Regenerate `external/patchsets/<component>/<version>/` with `git format-patch`.
6. Update `external/patchsets/README.md` and the component-specific README.
7. Run `./scripts/validate-patchsets.sh` and require all tree comparisons
   to report `OK`.
8. Run the component build, instruction scan, and qemu-pc98 boot tests
   before committing the new gitlink.
