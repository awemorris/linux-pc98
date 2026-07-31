# glibc 2.41 genuine-i386 patch set

Apply to the pristine `upstream-2.41` snapshot:

1. `0001-Add-Linux-assisted-atomics-for-genuine-i386.patch`
   - Adds the opt-in `--enable-i386-kernel-atomics` configuration for an
     exact `i386-linux-gnu` target.
   - Uses the versioned Linux `i386_atomic` service for operations which an
     Intel 80386 cannot implement natively.
   - Retains native memory `XCHG` and 386-safe locked arithmetic where
     possible.
   - Removes exact-i386 dependencies on CPUID, `PAUSE`, later FPU state
     instructions, and native compare-and-exchange.
   - Leaves i486 and later targets on the ordinary glibc atomic and CPU
     feature paths.

Recorded source state:

- upstream commit: `88bdb2beb33cfd95defd85fcfebae4e54023c735`;
- port commit: `cddb5b71c3ee05561cc78ed9c7f8f1e04f703d68`;
- resulting tree: `be16c4a3a3d7876acbc4d9e569c833c7981689da`;
- patch SHA-256:
  `a4e6e31db5bbb636bca5ff161e8777e002ca2a74f961f4e458f98551b38b33ec`.

Validated output:

- glibc 2.41 loader, libc, libm, tests, and dynamic BusyBox built with
  `-march=i386 -mtune=i386`;
- qemu-pc98 boot with `-M pc9801 -cpu 386`, dynamic `/sbin/init`, glibc
  smoke suite, and all 12 kernel-atomic TAP tests;
- corresponding ordinary i486 build and qemu-pc98 runtime pass;
- no post-386 opcode or unresolved compiler-atomic helper in the scanned
  i386 runtime artifacts.

The port requires the matching version-1 kernel UAPI and syscall 472 from
the linux-pc98 Linux 7.1 tree. It is not a standalone userspace emulation of
missing 80386 atomic instructions. See `I386-PORT.md` in the glibc submodule
and `GLIBC-2.41-I386-PORT-PLAN.md` in the parent repository for design and
remaining production gates.
