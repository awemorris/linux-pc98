# GCC 14.3.0 legacy-i386 patch set

The validated compiler baseline is the unmodified GCC 14.3.0 release.
`0001-Document-the-legacy-i386-validation-baseline.patch` only adds the
maintenance document stored in the `gcc-i386` repository.

Buildroot selects the existing GCC i386 backend with:

```text
-march=i386 -mtune=i386
```

Do not describe GCC as locally patched until a functional source change is
added after the `upstream-14.3.0` snapshot. GCC 14.4 should be treated as a
separate upstream merge and must pass the complete i386 output scan and
qemu-pc98 boot test before replacing this baseline.
