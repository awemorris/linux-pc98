# Toolchain patch inventory

This directory mirrors the exact differences carried by the toolchain
submodules. Patch files are ordered and can be applied with `git am`.

## Current and retained baselines

| Component | Release archive SHA-256 | Upstream snapshot | Submodule revision | Functional patches |
|---|---|---|---|---|
| GCC 14.3.0 | `e0dc77297625631ac8e50fa92fffefe899a4eb702592da5c32ef04e2293aca3a` | `19404ec3e5dca0950f99dba1fae00d4cf09545f1` | `a0e5e713daa14252912d420a2aaa3107fc874a80` | None; documentation commit only |
| musl 1.2.6 | `d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a` | `0170a66e2ddab6e1622fd728dc6388567226d228` | `78c0972e439e7473f8660655fed5c24db5a929d5` | Two; plus one documentation commit |
| glibc 2.41 (active port) | `f24aa441021121a79266f0d75242706cab8843a47901fefe74527491807f1998` | `88bdb2beb33cfd95defd85fcfebae4e54023c735` | Pending review/commit | Working genuine-i386 port; patch export must wait for the submodule commit |
| glibc 2.43 (retained research baseline) | `d9c86c6b5dbddb43a3e08270c5844fc5177d19442cf5b8df4be7c07cd5fa3831` | `a7dfa8c69763bfc6d222045ab00d4c7840335c91` | `2a728b1560b217bfc14bd6c316d41f3276cbbbea` | None; documentation commit only |

## Regenerating exports

Run these commands from the linux-pc98 repository root:

```sh
rm -f toolchain/patchsets/musl/1.2.6/*.patch
git -C toolchain/musl format-patch \
  --output-directory "$PWD/toolchain/patchsets/musl/1.2.6" \
  upstream-1.2.6..main

rm -f toolchain/patchsets/gcc/14.3.0/*.patch
git -C toolchain/gcc format-patch \
  --output-directory "$PWD/toolchain/patchsets/gcc/14.3.0" \
  upstream-14.3.0..main

rm -f toolchain/patchsets/glibc/2.43/*.patch
git -C toolchain/glibc format-patch \
  --output-directory "$PWD/toolchain/patchsets/glibc/2.43" \
  upstream-2.43..main
```

The documentation-only GCC and retained glibc 2.43 patches are intentionally
exported: they make those revisions exactly reproducible.

The active glibc 2.41 working tree cannot be exported faithfully until it is
committed in the submodule. After review and commit, create the 2.41 directory
and export the whole port series with:

```sh
mkdir -p toolchain/patchsets/glibc/2.41
rm -f toolchain/patchsets/glibc/2.41/*.patch
git -C toolchain/glibc format-patch \
  --output-directory "$PWD/toolchain/patchsets/glibc/2.41" \
  upstream-2.41..port/glibc-2.41-i386
```

Then replace the pending revision in the table, add the component README, and
change `validate-patchsets.sh` from the retained 2.43 entry to:

```sh
validate glibc 2.41 upstream-2.41
```

## Mechanical validation

After changing a submodule or regenerating an export, run:

```sh
./toolchain/validate-patchsets.sh
```

The script starts at each recorded upstream tag, applies every exported
patch with `git am`, and compares the resulting Git tree ID with the pinned
submodule tree. A successful result proves that the inventory is complete
and in the correct order. It does not replace the component-specific build,
instruction-scan, or qemu-pc98 runtime tests.
