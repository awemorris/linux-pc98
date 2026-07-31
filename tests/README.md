# glibc runtime tests

`glibc-i386-smoke.c` is shared by the exact-i386 and i486 validation builds.
It tests the dynamic loader and glibc 2.41 through:

- malloc/free;
- `dlopen`/`dlsym` of libm;
- per-thread TLS;
- pthread creation, join, and mutex serialization;
- fork;
- a process-shared robust mutex whose owner exits while holding the lock.

Build it with `../build-glibc-tests.sh i386` or
`../build-glibc-tests.sh i486`. The exact-i386 build is complemented by the
TAP selftest in
`../linux-7.1/tools/testing/selftests/x86/i386_atomic.c`.

The qemu-pc98 runtime test root must use the matching staged dynamic loader
and libraries. The validated configuration uses a dynamically glibc-linked
BusyBox as `/sbin/init`, not a musl init process.
