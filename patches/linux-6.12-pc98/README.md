# Linux 6.12 PC-98 Patch Inventory

This directory preserves two views of the Linux 6.12 PC-98 work.

## `0001` through `0020`

The numbered patches are the original hardware-enablement sequence recovered
from the pre-restructure development tree. They apply cleanly, in order, to
the official Linux v6.12 commit:

```text
adc218676eef25575469234709c2d87185ca223a
```

They are useful for reading the implementation chronologically:

```sh
git checkout -b pc98-v6.12 adc218676eef25575469234709c2d87185ca223a
git am /path/to/patches/linux-6.12-pc98/00*.patch
```

The series was replay-tested with `git am` and passed `git diff --check`.

## `current-complete.patch`

The numbered series predates several corrections and additions now present
in `linux-6.12/`, notably the NEC98 partition parser and later keyboard,
GDC-console, and Trident framebuffer work. `current-complete.patch` is the
authoritative single diff from official v6.12 to the current
`linux-6.12/` directory.

- Files changed: 34
- Insertions: 3,028
- Deletions: 7
- SHA-256:
  `ea498692ec4b131e402f43760d3dc00ef3d02e5266471dbc4395bdc22b3e0269`

Apply it to a clean v6.12 tree with:

```sh
git checkout -b pc98-v6.12 adc218676eef25575469234709c2d87185ca223a
git apply --check /path/to/current-complete.patch
git apply /path/to/current-complete.patch
```

The source directory remains the maintained truth. Regenerate the complete
patch after changing `linux-6.12/`; do not append a correction manually and
leave the snapshot stale.

See `../../LINUX-6.12-PORT.md` for provenance, design decisions, subsystem
mapping, test status, and known omissions.
