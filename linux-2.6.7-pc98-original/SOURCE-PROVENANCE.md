# Historical Linux/PC-98 Source Provenance

This directory is an unmodified source snapshot of the last complete
PC-9800 subarchitecture in the official pre-Git Linux kernel history, plus
this provenance file.

## Snapshot

- Repository:
  <https://git.kernel.org/pub/scm/linux/kernel/git/history/history.git>
- Commit: `b429f3b3c68296611626c926a78f6d5fe3760226`
- Tree: `566f0a0b5d388d0cd6cf9a796378470e7ad602b5`
- `git describe`: `v2.6.7-33-gb429f3b3c6`
- Kernel version from `Makefile`: `2.6.7`
- Snapshot date: 2004-06-17
- SHA-256 of `git archive --format=tar <commit>`:
  `0a6712a4681e30fb222ed22cf826e57c56df30f4fe39189a656b3ffa70a5cc9a`

The next commit,
`5e018f7e60c98df93ff39246c3132dbc985aae8e` (`[PATCH] Remove PC9800
support`), started disconnecting the port from Kconfig and the build. Later
commits removed the remaining platform and driver files, and
`df13449018c3ae8119cf1daae1fffda5b47231f3` finally removed the `boot98`
decompressor. Selecting the parent of the first removal therefore preserves
the coherent upstream implementation rather than a partially removed tree.

## Reproduction

```sh
git clone \
  https://git.kernel.org/pub/scm/linux/kernel/git/history/history.git \
  linux-history
mkdir linux-2.6.7-pc98-original
git -C linux-history archive \
  b429f3b3c68296611626c926a78f6d5fe3760226 |
  tar -x -C linux-2.6.7-pc98-original
```

Remove this `SOURCE-PROVENANCE.md` file before comparing the directory with
the archive checksum above.

## License and use

The snapshot retains the original kernel `COPYING` file and source notices.
It is reference material, not a build input for the maintained kernels. The
modern port does not blindly copy the old code: kernel subsystem APIs,
booting, interrupt setup, storage, input, serial, and timekeeping have all
changed substantially since Linux 2.6.7.

See the repository-level `LINUX-6.12-PORT.md` for the old-to-new subsystem
mapping, the reconstruction sequence, validation, and known omissions.
