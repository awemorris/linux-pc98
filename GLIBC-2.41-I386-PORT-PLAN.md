# Debian 13 glibc 2.41 genuine i386 port plan

Status: initial genuine-i386/i486 runtime port implemented and validated;
Debian packaging and the full upstream test suite remain  
Target repository: `~/linux-pc98` and its `toolchain/glibc` submodule  
Target CPU: genuine Intel 80386 instruction set (`-march=i386`), not merely
Debian's architecture named `i386`  
Target OS: the Linux 7.1 i386/PC-98 port in this repository  
Primary validation machine: qemu-pc98, `-M pc9801 -cpu 386`  
Document date: 2026-07-31

## 1. Objective

Port the Debian 13 version of glibc to a genuine 80386, while preserving the
existing i386 GNU/Linux ABI:

- ELF machine remains `EM_386`.
- Debian architecture remains `i386`.
- GNU triplet remains `i386-linux-gnu`.
- Dynamic loader remains `/lib/ld-linux.so.2`.
- Public structure layouts, symbol versions, TLS ABI, pthread ABI, `long
  double` ABI, syscall ABI, and library SONAMEs must not be changed.
- The result must run with qemu-pc98 `-M pc9801 -cpu 386`.
- PC-98-specific hardware behavior must not be introduced into glibc.
  The libc port should also be usable by another genuine i386 Linux port.

The immediate deliverable is a working glibc and a small glibc-linked
root filesystem. The eventual project goal is a private Debian 13 binary
archive whose `Architecture: any` packages have all been rebuilt for a
genuine i386 baseline.

## 2. Executive conclusion

This is feasible, but removing the configure-time rejection is not the port.
glibc 2.41 relies on compare-and-exchange throughout NPTL, malloc, the dynamic
loader, NSS, and miscellaneous initialization code.

An 80386 provides useful atomic primitives:

- `XCHG r/m8|16|32,r8|16|32` (a memory `XCHG` is implicitly locked);
- `LOCK ADD/ADC/SUB/SBB/AND/OR/XOR/INC/DEC/NEG/NOT`;
- `LOCK BTS/BTR/BTC`.

It does not provide:

- `CMPXCHG`;
- `XADD`;
- `CMPXCHG8B`;
- `BSWAP`;
- `CPUID`;
- `RDTSC`;
- conditional moves, FXSAVE, MMX, or SSE.

`XCHG` is sufficient to implement a lock. It is not, by itself, a drop-in
implementation of an arbitrary compare-and-exchange on an existing public
pthread state word. Replacing all current NPTL state machines with new
test-and-set algorithms is possible in principle, but it has a much larger
ABI, process-shared synchronization, robust mutex, cancellation, and
correctness surface than a first port should accept.

The recommended first implementation therefore is:

1. keep glibc's current NPTL algorithms and public ABI;
2. execute loads, stores, barriers, `XCHG`, and non-fetching locked arithmetic
   natively on the 80386;
3. provide a small, explicitly enabled Linux kernel service for operations
   that genuinely need an observed old value or compare-and-exchange;
4. make the genuine-i386 glibc atomic backend call that service;
5. disable glibc CPU multiarch implementations in the first Debian build;
6. run the complete glibc test suite and dedicated process-shared stress tests;
7. optimize syscall-backed atomics later, without changing the ABI.

The first implementation should use a small system call, not an invalid-opcode
instruction emulator. A syscall has a smaller and auditable attack surface
than decoding arbitrary faulting x86 instructions in the `#UD` handler.
Historical Debian kernels did emulate `BSWAP`, `XADD`, and `CMPXCHG` for an
80386, but contemporary discussion also records security problems in that
approach. The old patch is valuable design evidence, not code to apply
unchanged.

## 3. Exact source baselines

### 3.1 Target Debian 13 source

At the time of this plan, Debian 13 stable provides:

| Item | Value |
|---|---|
| Source package | `glibc` |
| Version | `2.41-12+deb13u3` |
| Salsa packaging tag | `debian/2.41-12+deb13u3` |
| Peeled packaging commit | `5970a557f655bd9a89eb0f03246751d38368cb2d` |
| `.dsc` SHA-256 | `aa1ab10010fcf169454a5c6a123094a3997392922593d86a3a5adc180a07ca40` |
| upstream tar SHA-256 | `f24aa441021121a79266f0d75242706cab8843a47901fefe74527491807f1998` |
| Debian tar SHA-256 | `de7d715bf7e559b78baebac4115122641842f65faf0a5080a55954877a55cebe` |

The exact source must be obtained with:

```sh
apt-get source glibc=2.41-12+deb13u3
```

Do not develop against the currently checked-out glibc 2.43 submodule and
then declare Debian 13 compatibility. A generic patch may be forward-ported
to 2.43 after the Debian 13 build works.

Debian has a `2.41-12+deb13u4` packaging tag in proposed updates. Before the
first implementation commit, check `apt-cache policy libc6` again. If `u4`
has entered stable, materialize both `u3` and `u4`, review the delta, and
change this document and the lock file deliberately. Do not silently build
whichever version `apt-get source glibc` happens to select.

### 3.2 Historical genuine-i386 Debian reference

The historical tree to preserve is Debian Woody:

| Item | Value |
|---|---|
| Source package version | `glibc 2.2.5-11.8` |
| Debian suite | `woody` |
| Thread implementation | LinuxThreads, not modern NPTL |
| `.dsc` SHA-256 | `aa6b2eac41d32a76feb32b17958bc344cb50ab28832feda32d513311cee1d57d` |
| original tar SHA-256 | `124452d2b6bf06e80ef0b0bfcad2c67959c5b27c163dc21afa9ac87444397331` |
| Debian diff SHA-256 | `98918e9a03bbe6bc0d842045349ef2ba8e46b0894f906db62699e550fd47c89f` |

Archive URLs:

```text
https://archive.debian.org/debian/pool/main/g/glibc/glibc_2.2.5-11.8.dsc
https://archive.debian.org/debian/pool/main/g/glibc/glibc_2.2.5.orig.tar.gz
https://archive.debian.org/debian/pool/main/g/glibc/glibc_2.2.5-11.8.diff.gz
```

This is a useful genuine-80386 Debian reference because:

- its packaging uses the compiler's then-i386 default and no i486 baseline;
- its changelog explicitly records removal of a once-explicit
  `-march=i386` because it was no longer needed;
- Debian changed its default to i486 later, in the wake of the GCC 3.3
  C++ atomicity problem;
- in 2005 Debian changed its GNU Linux architecture string from
  `i386-linux` to `i486-linux-gnu`, documenting the actual minimum.

It is not an implementation base for glibc 2.41. It predates NPTL, modern TLS,
time64, modern dynamic-loader hardening, and two decades of ABI additions.
Use it to answer narrow historical questions:

- how baseline i386 assembly avoided post-386 instructions;
- how the old loader and LinuxThreads bootstrapped;
- which ABI conventions already existed;
- whether a suspicious modern assumption has an older i386 implementation.

### 3.3 Upstream transition reference

Also pin these upstream points:

| Reference | Commit |
|---|---|
| upstream glibc 2.17 tag | `e30441d7ab4195028b3d6925d5a1ae544c6685d8` |
| upstream glibc 2.18 tag | `f470138ba1124af9cffb839912538bbab2d0ca07` |
| explicit i386 rejection commit | `a01f19c8fb12eef419d4112879bc715e2ab6f6d7` |

The rejection commit was made on 2013-04-06. It maps an `i386-*` target to
i686 and rejects a compiler that cannot inline
`__sync_val_compare_and_swap`. Its own explanation says that current NPTL
and glibc atomics require operations absent on an 80386.

glibc 2.17 is a useful structural bridge but is not a hidden working NPTL
i386 implementation. Its 32-bit x86 atomic backend already depends on the
i486 `CMPXCHG` implementation. Therefore the forward-port history is:

```text
Debian Woody 2.2.5 (real i386, LinuxThreads)
        |
        | historical ABI/assembly reference only
        v
glibc 2.17 (modernizing bridge, but already i486 atomic assumptions)
        |
        | commit a01f19c8 makes the existing limitation explicit
        v
glibc 2.18 ... 2.41 (NPTL and widespread compiler atomics)
```

## 4. Repository organization to create before code changes

### 4.1 `glibc-i386` repository

Do not copy multiple complete versions into the same working tree. Preserve
them as Git refs:

```text
awemorris/glibc-i386
  refs/tags/upstream-2.41
  refs/tags/upstream-2.43
  refs/heads/port/glibc-2.41-i386
  refs/heads/port/glibc-2.43-i386        # later, not part of first success
  refs/heads/historical/debian-woody-2.2.5-11.8
```

Import the expanded Woody source into the historical branch as one
source-import commit. Woody's source package contains nested
`glibc-2.2.5.tar.bz2` and LinuxThreads tarballs, so `dpkg-source -x` alone is
not the expanded libc tree. In a disposable directory, run the package's
`prep.sh`/`debian/rules unpack` and Debian patch target, verify the expected
`glibc-2.2.5/` tree, then import that tree together with the Debian packaging
and a manifest. Record all three archive URLs and SHA-256 values in the
commit message and in `HISTORICAL-SOURCES.md`. Preserve the original nested
archives by hash in the manifest; they need not be duplicated as Git blobs.
Do not merge the historical branch into the port branch.

The active submodule gitlink in `linux-pc98` should point to a commit on
`port/glibc-2.41-i386` while the Debian 13 port is under development.

### 4.2 Main `linux-pc98` repository

Create this layout:

```text
toolchain/
  glibc/                              # submodule
  patchsets/glibc/2.41/
    README.md
    source.lock
    0001-...
  references/glibc/
    README.md
    debian-woody-2.2.5-11.8.lock
    upstream-2.17.lock
  references/kernel/
    README.md
    debian-x86-i486-emulation/
      SOURCE.md
      x86-i486_emu.dpatch             # historical reference, never auto-applied
  scripts/
    fetch-glibc-2.41-debian.sh
    build-glibc-i386.sh
    package-glibc-i386-debian.sh
    check-i386-opcodes.py
    run-glibc-tests-pc98.sh
  tests/glibc-i386/
    ...
```

`source.lock` must contain version, URLs, SHA-256 values, Salsa tag and commit,
glibc-i386 base commit, patch head commit, kernel UAPI version, compiler
version, and binutils version. A build must stop if a hash differs.

The submodule history is authoritative. `toolchain/patchsets/glibc/2.41/`
must be regenerated with `git format-patch` and verified in the same way as
the existing GCC and musl inventories.

### 4.3 Commit boundaries

Use small commits in this order:

1. historical source refs and source lock only;
2. kernel UAPI and atomic service plus self-tests;
3. kernel futex correction for `CONFIG_M386`;
4. glibc configure/sysdeps selection only;
5. glibc atomic backend only;
6. CPUID/ISA baseline and spinlock assembly only;
7. glibc smoke-test harness and opcode scanner;
8. upstream glibc test results and fixes, one issue per commit;
9. Debian packaging profile;
10. Debian glibc packages and rootfs integration scripts.

Do not combine the kernel service, glibc port, Debian packaging, and generated
disk image in one commit.

## 5. Verified glibc 2.41 findings

### 5.1 Configure rejection

`sysdeps/i386/configure.ac` has two independent rejections:

1. `config_machine == i386` is rejected explicitly.
2. a compiler probe requires inline `__sync_val_compare_and_swap`.

Both must be addressed. Merely deleting the first block still fails the
second block or causes unresolved compiler atomic helper calls.

### 5.2 Sysdeps and reported minimum ISA

`sysdeps/i386/preconfigure` maps only `i[4567]86` to subdirectories such as
`i386/i486`. An exact `i386` has no separate current CPU subdirectory.

`sysdeps/i386/isa.h` says:

```c
#define MINIMUM_ISA 486
```

A genuine-i386 build needs its own sysdeps leaf and `MINIMUM_ISA 386`.
Do not globally change the existing i486 header, because that would
misdescribe ordinary Debian i486/i686 builds.

### 5.3 Atomic backend

`sysdeps/x86/atomic-machine.h` currently:

- sets `USE_ATOMIC_COMPILER_BUILTINS` to 1;
- uses compiler `__sync`/`__atomic` compare-exchange and fetch-add builtins;
- contains inline `CMPXCHG`;
- contains inline `XADD`;
- already has a native `XCHG` implementation;
- sets `__HAVE_64B_ATOMICS` to 0 for i386 ABI builds.

The generic `include/atomic.h` can operate with
`USE_ATOMIC_COMPILER_BUILTINS == 0`, provided an architecture supplies the
fundamental compare-exchange, exchange, barriers, and required fetch
operations. This is the correct extension point. Do not patch hundreds of
call sites to invoke a kernel service directly.

A source scan of the Debian 2.41 tree found atomic references in:

| Area | Files containing relevant atomic operations |
|---|---:|
| `sysdeps` | 84 |
| `nptl` | 53 |
| `nscd` | 12 |
| `elf` | 10 |
| `stdlib` | 9 |
| `malloc` | 6 |
| `resolv` | 4 |
| `nss` | 4 |
| `misc` | 4 |

The exact numbers include tests and non-i386 sysdeps, but the distribution is
enough to reject a narrow “pthread mutex only” implementation.

NPTL uses CAS/fetch operations in:

- ordinary, recursive, error-checking, robust, PI, and PP mutex paths;
- condition variables and their wide counters;
- reader/writer locks;
- barriers;
- POSIX semaphores;
- pthread once;
- create, join, detach, cancellation, and setxid paths;
- pthread keys;
- spin locks.

### 5.4 64-bit atomics

The i386 psABI does not guarantee sufficient alignment for general 64-bit
atomics, and current glibc already sets `__HAVE_64B_ATOMICS` to 0 on i386.
NPTL contains alternate 32-bit algorithms for semaphores and wide counters.

The initial port must keep `__HAVE_64B_ATOMICS == 0`. Do not invent a new
alignment requirement and do not change the size or alignment of public
pthread objects.

### 5.5 TLS

`sysdeps/i386/nptl/tls.h` uses:

- `%gs` as the thread pointer segment;
- `set_thread_area` to install a GDT descriptor;
- the existing i386 TCB and DTV layouts.

These mechanisms do not intrinsically require an i486 instruction. The Linux
kernel must provide working `set_thread_area`, `clone`, and futex syscalls on
the i386 port. No public TLS ABI redesign is planned.

TLS descriptors have FNSAVE, FXSAVE, and XSAVE resolver variants. On a CPU
without CPUID/FXSR, the resolver must select the FNSAVE path. This is a test
requirement, not a reason to delete the optimized variants from all x86
builds.

### 5.6 CPUID and IFUNC selection

`sysdeps/x86/cpu-features.c` already has a no-CPUID path using
`__get_cpuid_max`. GCC's `cpuid.h` tests the EFLAGS ID bit before executing
`CPUID`.

However, `sysdeps/x86/include/cpu-features.h` currently classifies:

- explicit `__i486__` as `HAS_CPUID 0`;
- explicit i586/i686 as having CPUID;
- the final fallback as `HAS_CPUID 1`, `HAS_I586 1`, `HAS_I686 1`.

A compiler using only `__i386__` falls into that unsafe final fallback. Add
an explicit genuine-i386 branch with:

```c
#define HAS_CPUID 0
#define HAS_I586 HAS_ARCH_FEATURE (I586)
#define HAS_I686 HAS_ARCH_FEATURE (I686)
```

Then verify that the no-CPUID path leaves all post-386 features inactive and
selects no i486/i586/i686 IFUNC implementation.

The first production build must use `--disable-multi-arch`. Re-enable glibc
CPU multiarch only after the baseline build has passed and an IFUNC-specific
test proves that optional post-386 implementations are never selected on an
80386.

### 5.7 Assembly exception

`sysdeps/i386/pthread_spin_trylock.S` contains `CMPXCHG`. Provide a
genuine-i386 override using a memory `XCHG`:

1. place the candidate locked value in a register;
2. `xchg` it with the lock word;
3. return success only when the observed value was zero;
4. otherwise return `EBUSY`.

This operation needs test-and-set semantics, not arbitrary CAS semantics, so
`XCHG` is the direct i386 implementation.

### 5.8 Floating point

The GNU i386 hard-float ABI and glibc libm use x87 instructions. The initial
target is therefore:

```text
80386 CPU + 80387-compatible floating-point execution
```

This may be a physical 80387 or correct kernel/emulator support. A soft-float
ABI would not be Debian's i386 ABI and is out of scope for this port. Test a
no-FPU physical machine separately before claiming support for it.

### 5.9 Debian 13's package named `i386`

Debian 13's remaining official i386 packages are not genuine-80386 binaries.
The Debian 13 release notes state that this co-architecture requires SSE2.
Debian 12 also used an i686 baseline. None of those binaries may be mixed
into the genuine-i386 runtime.

The private port may retain the Debian architecture string `i386`, but it
must use a separate suite and package archive. Reuse `Architecture: all`
packages; rebuild every `Architecture: any` package.

## 6. Atomic implementation decision

### 6.1 Candidate approaches

| Approach | Correctness surface | Performance | ABI impact | Recommendation |
|---|---|---:|---|---|
| Delete configure check and use compiler atomics | Incorrect/unlinkable on `-march=i386` | N/A | none | reject |
| Pretend the CPU is i486 | runs only with hidden instruction emulation | good when emulated | none | reject as an undocumented build |
| Rewrite all NPTL algorithms around TAS/`XCHG` | very large; robust/pshared/cancel/rwlock/condvar risk | potentially good | must preserve layouts | later research, not first port |
| Process-local global userspace lock | fails process-shared objects and has signal/fork hazards | poor | hidden semantic break | reject |
| Full `#UD` emulator for i486 instructions | decoder and kernel attack surface; historical security issues | trap overhead | none | later optimization only |
| Kernel atomic syscall used by glibc backend | small, explicit, auditable; correct pshared semantics | syscall overhead | new kernel dependency, no glibc public ABI change | first implementation |

### 6.2 Pure `XCHG`/test-and-set libc route: detailed assessment

A no-new-syscall port is not impossible. It is a separate NPTL port, rather
than an atomic-macro port.

Relatively straightforward replacements:

- internal libc locks whose entire state is an owned test-and-set word;
- `pthread_spin_lock`, trylock, and unlock;
- uncontended ordinary mutex acquisition if the state machine is redesigned
  around exchange;
- counters where the caller needs only a zero/nonzero result and a locked
  `INC`/`DEC` plus flags is sufficient.

Hard replacements:

- robust mutex owner words, which combine a TID with
  `FUTEX_OWNER_DIED`/waiter state and must follow the kernel robust-list ABI;
- PI and priority-protection mutexes, whose user word participates in futex
  operations;
- condition-variable generation counters and waiter reference fields;
- reader/writer locks with several state bits and concurrent readers;
- barriers and semaphores which return or branch on the observed old count;
- cancellation and thread lifecycle state machines;
- loader, malloc, NSS, and initialization call sites outside NPTL;
- every `PTHREAD_PROCESS_SHARED` object, because a hidden process-local lock
  does not serialize another process.

To pursue this route correctly, create i386-specific implementations for the
affected NPTL functions, preserve every public object size/offset, define a
formal state transition table for each primitive, and prove cancellation,
owner death, fork, and process-sharing behavior. The current source scan
identified dozens of CAS users and more than fifty NPTL files with atomics.
That project may eventually remove syscall overhead, but it is larger than
the glibc bring-up and is not suitable as the first lower-model task.

The syscall backend deliberately leaves this optimization open. Once the
test suite is green, profiles can identify hot operations. A future
i386-specific NPTL primitive may replace one syscall-backed state machine at
a time, with the syscall path retained as the correctness reference.

### 6.3 Invalid-opcode emulation route: detailed assessment

Historical Debian supported an i486-compiled userspace on an 80386 by
emulating instructions such as `BSWAP`, `XADD`, and `CMPXCHG` after `#UD`.
This can make current glibc's inline assembly work with fewer libc changes,
but a correct emulator has to handle:

- instruction prefixes, including `LOCK`, operand size, address size, and
  segment overrides;
- ModRM and SIB decoding and all valid 16/32-bit effective-address forms;
- user segment bases and limits;
- register input/output and exact EFLAGS semantics;
- instruction length and restart EIP;
- read, write, and execute faults with normal signal behavior;
- atomicity across preemption and process-shared mappings;
- validation that no user operand can address kernel memory;
- races with unmap, fork, signals, and ptrace.

This is a security-sensitive x86 decoder in the general-protection path.
It also risks hiding accidental i486 instructions from the opcode scanner.
It should be evaluated only after the explicit syscall version establishes
correct libc behavior and tests. If implemented later, restrict it to the
smallest encodings actually emitted, add hostile decoder tests, and retain
the syscall backend as a build-time fallback.

### 6.4 Why kernel participation is required

On a uniprocessor, preventing preemption and signal delivery is sufficient
to serialize a load/compare/store sequence. User mode cannot disable kernel
preemption. A process-local lock also cannot serialize a `PTHREAD_PROCESS_SHARED`
object used by another process unless the lock itself is shared and has
well-defined lifetime and recovery semantics.

The kernel already solves the same 80386 problem internally in this project:
`CONFIG_M386` has out-of-line `cmpxchg_386` and `xadd_386` helpers which
exclude interrupts around a UP load/modify/store sequence. User memory needs
additional fault handling and access validation, but the model is proven.

### 6.5 Kernel UAPI

Add an opt-in configuration:

```text
CONFIG_X86_USER_ATOMIC_386
  depends on X86_32 && M386 && !SMP
```

Add one system call with an intentionally versioned UAPI. The symbolic
interface should be equivalent to:

```c
long i386_atomic(void __user *address,
                 unsigned int operation,
                 unsigned long argument1,
                 unsigned long argument2);
```

The exact syscall number must be selected by inspecting the target
`syscall_32.tbl` at implementation time. Never reuse an obsolete syscall
number silently. Keep the number in generated UAPI headers and make glibc's
configure check require it.

Required operations:

| Operation | Input | Return |
|---|---|---|
| `CMPXCHG` | expected, desired | observed old value |
| `XADD` | increment | observed old value |
| `FETCH_AND` | mask | observed old value |
| `FETCH_OR` | mask | observed old value |
| `FETCH_XOR` | mask | observed old value |

Required widths are 1, 2, and 4 bytes. No 8-byte operation is allowed in the
first ABI.

Encoding rules:

- operation and width use named UAPI constants, not magic numbers duplicated
  in glibc;
- the UAPI gets a version constant;
- unknown operations or widths return `-EINVAL`;
- an address must be naturally aligned for its width;
- a bad or inaccessible user address returns `-EFAULT`;
- valid operations return the zero-extended observed value;
- wrapping integer arithmetic is intentional.

The inline glibc backend reads raw `%eax`; it must not apply the normal libc
`-4095..-1` errno conversion, because an observed 32-bit atomic value can
legitimately have those bit patterns. Error returns exist for diagnostics
and hostile UAPI tests. A glibc atomic call is only made with a valid aligned
object, so it does not need to distinguish `0xfffffff2` as data from
`-EFAULT`. If reviewers require unambiguous errors, change the UAPI before
implementation to return status separately through a versioned argument
structure; do not change it after binary packages have been released.

### 6.6 Kernel-side implementation constraints

Implement in a new file such as:

```text
linux-7.1/arch/x86/kernel/i386_user_atomic.c
```

The algorithm must:

1. reject unsupported configuration, operation, width, and alignment;
2. validate the user range;
3. prefault the writable user range outside the atomic critical section;
4. disable page faults in the critical section;
5. hold a kernel-global raw lock or equivalent UP preemption/IRQ exclusion
   that also serializes calls made by different processes;
6. perform exactly one load/condition/store or load/RMW/store;
7. restore state on every exit;
8. retry prefaulting if the in-atomic user access reports a fault;
9. place explicit compiler/memory barriers at the UAPI boundary;
10. never dereference a user-selected kernel address;
11. never decode or execute bytes supplied by user space.

Expected control flow, using the Linux 7.1 API names that actually exist in
that tree:

```text
validate opcode/width/alignment/access_ok
retry:
    fault_in_writeable(user_address, width) outside lock
    raw_spin_lock_irqsave(global_i386_user_atomic_lock)
    pagefault_disable
    in-atomic get_user(width)
    if successful:
        compute observed/new value in kernel locals
        in-atomic put_user(width) only when the operation writes
    pagefault_enable
    raw_spin_unlock_irqrestore
    if successful:
        return observed bits
    if the in-atomic access faulted and retry budget remains:
        goto retry
    return -EFAULT
```

Do not call ordinary `copy_to_user` or `copy_from_user` while holding a raw
spinlock. Do not allow a page fault or allocation while the serialization
lock is held. Review the actual Linux 7.1 uaccess helpers before naming the
private function; API names have changed across kernel versions.

Do not copy the old Debian invalid-opcode emulator. Preserve it only as a
reference and document the security failure before borrowing any idea.

### 6.7 Futex correction

Audit and patch:

```text
linux-7.1/arch/x86/include/asm/futex.h
```

The current Linux 7.1 i386 port has kernel-internal `cmpxchg_386`, but the
futex in-atomic user operation paths still contain CPU `CMPXCHG`. Robust
mutexes, PI mutexes, condition variables, and process-shared synchronization
can therefore fail even if the new glibc atomic syscall is correct.

For `CONFIG_M386`, route:

- `arch_futex_atomic_op_inuser`;
- `futex_atomic_cmpxchg_inatomic`;

through the same validated user-memory atomic core. Do not invoke the user
syscall from inside the kernel. Share a private kernel helper.

### 6.8 Kernel self-tests

Add `tools/testing/selftests/x86/i386_atomic.c` and require:

- CAS success and failure for 8/16/32 bits;
- XADD wraparound for all widths;
- AND/OR/XOR return-value and final-value checks;
- bad pointer, read-only mapping, misalignment, invalid op, and invalid width;
- two pthreads contending on one word;
- parent/child contention on a `MAP_SHARED` word;
- signal delivery during one million operations;
- unmap/remap pressure from another thread without kernel fault or corruption;
- futex wait/wake, robust mutex owner death, and process-shared mutex tests.

Run once with `-cpu 386` and once with an i486/i686 control kernel. The new
syscall should return `ENOSYS` when the kernel is built without the config
option.

## 7. glibc source design

### 7.1 New genuine-i386 sysdeps leaf

Modify `sysdeps/i386/preconfigure`:

```sh
case "$machine" in
i386)       base_machine=i386 machine=i386/i386 ;;
i[4567]86)  base_machine=i386 machine=i386/$machine ;;
esac
```

Create:

```text
sysdeps/i386/i386/isa.h
sysdeps/i386/i386/pthread_spin_trylock.S
sysdeps/unix/sysv/linux/i386/i386/atomic-machine.h
```

If configure's printed sysdeps order does not select the Linux-specific
atomic header first, stop and correct the sysdeps layout. Do not work around
the error by replacing the shared `sysdeps/x86/atomic-machine.h`.

`sysdeps/i386/i386/isa.h` contains:

```c
#define MINIMUM_ISA 386
```

### 7.2 Configure option and hard failure

Add an option named:

```text
--enable-i386-kernel-atomics
```

Default is `no`.

For `config_machine == i386`:

- fail unless the option is explicitly enabled;
- compile-check that `<asm/unistd.h>` defines the atomic syscall;
- compile-check the UAPI version and operation constants;
- define `HAVE_I386_KERNEL_ATOMICS`;
- skip only the `__sync_val_compare_and_swap` inline requirement.

For i486, i586, i686, and i786:

- preserve the existing compiler builtin check;
- reject `--enable-i386-kernel-atomics` as meaningless or ignore it with a
  clear configure notice;
- do not change the generated code.

Regenerate `configure` from `configure.ac` using the version expected by the
Debian package. Commit both source and generated file in the same commit.

Do not add an option that silently produces a single-thread-only libc. Modern
glibc has pthread symbols integrated into libc, and Debian packages expect
working NPTL.

### 7.3 Atomic header contract

The genuine-i386 `atomic-machine.h` must define at least:

```text
USE_ATOMIC_COMPILER_BUILTINS = 0
__HAVE_64B_ATOMICS = 0
ATOMIC_EXCHANGE_USES_CAS = 0
atomic_compare_and_exchange_val_acq
atomic_compare_and_exchange_val_rel
atomic_exchange_acq
atomic_exchange_rel
atomic_exchange_and_add
atomic_exchange_and_add_acq
atomic_exchange_and_add_rel
atomic_read_barrier
atomic_write_barrier
atomic_full_barrier
```

Recommended mapping:

| glibc primitive | i386 implementation |
|---|---|
| relaxed/acquire load, relaxed/release store | normal aligned load/store plus compiler barrier as required |
| exchange | memory `XCHG` |
| compare/exchange | atomic syscall `CMPXCHG` |
| fetch-add | atomic syscall `XADD` |
| fetch-and/or/xor | atomic syscall operation, or generic CAS loop after correctness is established |
| add/inc/dec without old value | initially generic; later optimize with `LOCK` arithmetic |
| fences | compiler barrier; syscall and locked operations are full hardware barriers on this target |
| spin nop | empty operation; there is no `PAUSE` on 80386 |

Prefer a minimal correct backend first. Optimize native `LOCK ADD/INC/DEC`
and bit operations in a separate commit after the full tests pass.

The syscall inline assembly must:

- use `int $0x80`;
- preserve PIC's `%ebx` correctly;
- use a `"memory"` clobber;
- not depend on a PLT call or TLS;
- be usable by `ld.so` before normal relocation;
- not link against `libatomic`, libgcc atomic helpers, or another DSO.

Avoid an out-of-line libc helper in the first backend: the dynamic loader and
early libc initialization need atomics before ordinary inter-object calls are
safe.

### 7.4 CPUID fix

Modify:

```text
sysdeps/x86/include/cpu-features.h
```

Add an explicit `__i386__` branch before the current final fallback.
Do not change i486/i586/i686 behavior.

No source change should initially be needed in
`sysdeps/x86/cpu-features.c`. Add tests that prove:

- `__get_cpuid_max` returns zero under qemu-pc98 `-cpu 386`;
- no `CPUID` instruction is executed after that result;
- `dl_platform` is not `i586` or `i686`;
- FNSAVE TLS resolver is selected;
- all post-386 IFUNC capability bits remain clear.

### 7.5 Spin lock override

Implement `pthread_spin_trylock` with `XCHG` in the new i386 leaf. Confirm
symbol version and aliases match the current implementation exactly. Do not
modify the public `pthread_spinlock_t` layout.

The C implementations of `pthread_spin_lock` and unlock can use the generic
atomic backend initially. A later optimization may use `XCHG` directly.

### 7.6 Files that should not change in the first port

Do not change these unless a failing test proves a need:

```text
nptl/pthread_mutex_*.c
nptl/pthread_cond_*.c
nptl/pthread_rwlock_*.c
nptl/sem_*.c
sysdeps/i386/nptl/tls.h
sysdeps/i386/dl-machine.h
sysdeps/i386/dl-tlsdesc*.S
sysdeps/i386/tls_get_addr.S
sysdeps/x86/cpu-features.c
public pthread headers
ABI list files
Versions files
```

This is a key review invariant. Changes to those files mean the implementation
has escaped the planned atomic abstraction and needs a design review.

### 7.7 File/function implementation matrix

This is the mechanical change list for the implementation agent.

#### glibc-i386

| File | Function/macro/section | Required change | Direct validation |
|---|---|---|---|
| `sysdeps/i386/configure.ac` | current i386 rejection and builtin probe | add the opt-in kernel-atomic option; require UAPI for exact i386; retain builtin probe for i486+ | configure negative and positive cases |
| `sysdeps/i386/configure` | generated output | regenerate, never hand-edit alone | diff corresponds to `configure.ac` |
| `sysdeps/i386/preconfigure` | machine `case` | select `i386/i386` for exact i386 | saved `config.make` sysdeps list |
| `sysdeps/i386/i386/isa.h` | new file | set `MINIMUM_ISA 386` | preprocessor assertion and ELF property check |
| `sysdeps/unix/sysv/linux/i386/i386/atomic-machine.h` | new file | non-builtin 8/16/32-bit backend and raw syscall inline | atomic microtests; no undefined atomic helper |
| `sysdeps/i386/i386/pthread_spin_trylock.S` | new override | implement trylock with `XCHG` and branches | spin API and contention tests |
| `sysdeps/x86/include/cpu-features.h` | `HAS_CPUID` classification block | add explicit `__i386__` no-CPUID case | `-cpu 386` loader test |
| `sysdeps/i386/Makefile` or selected test Makefile | test registration only | register i386 CPU/atomic/loader tests if needed | tests appear in `tests.sum` |
| `manual/install.texi` and/or `INSTALL` source | configure option documentation | document kernel dependency and UP-only status | generated docs contain option |
| `NEWS` | port note | state opt-in experimental genuine-i386 Linux support | human review |

If the Linux-specific atomic header is not selected by sysdeps precedence,
move it to the smallest correct leaf proven by configure output. Do not
duplicate the implementation in two headers.

#### Linux 7.1

| File | Function/section | Required change | Direct validation |
|---|---|---|---|
| `arch/x86/Kconfig.cpu` | `M386` vicinity | add `X86_USER_ATOMIC_386`, UP dependency and help | invalid configs rejected |
| `arch/x86/entry/syscalls/syscall_32.tbl` | next reviewed syscall slot | register `i386_atomic` | generated unistd number |
| `arch/x86/include/uapi/asm/i386_atomic.h` | new UAPI | version, operation, width constants | userspace header compile test |
| `arch/x86/kernel/i386_user_atomic.c` | new syscall and private core | validate, prefault, serialize, RMW, return observed | hostile and pshared self-tests |
| `arch/x86/kernel/Makefile` | conditional object list | build service only for config | symbol absent/present as expected |
| `arch/x86/include/asm/futex.h` | `arch_futex_atomic_op_inuser`, `futex_atomic_cmpxchg_inatomic` | call shared 386 user-memory core under `CONFIG_M386` | robust/PI/pshared futex tests |
| `tools/testing/selftests/x86/i386_atomic.c` | new test | UAPI, race, fault, signal, fork tests | TAP pass |
| `tools/testing/selftests/x86/Makefile` | test list | build/install test | `make run_tests` includes it |

The private kernel core may need a declaration in a new
`arch/x86/include/asm/i386_user_atomic.h`. It must not be placed in UAPI
unless user space needs it.

#### Debian package layer

| File | Required change | Direct validation |
|---|---|---|
| `debian/patches/series` | add port patches after the existing i386 patch group in deterministic order | `dpkg-source -x` succeeds |
| `debian/patches/i386/...` | exported/adapted glibc port commits | `quilt push -a` clean |
| `debian/sysdeps/i386.mk` | `legacy386` flags/options; suppress CPU multiarch | logged configure and compile commands |
| `debian/changelog` | local `+pc98i386N` version and scope | `dpkg-parsechangelog` |
| optional `debian/tests/...` | loader and NPTL smoke tests | autopkgtest in private rootfs |

Do not edit Debian's upstream `git-updates.diff`. Put the port after the
existing Debian patch queue so an update can replace `git-updates.diff`
without hiding local changes.

## 8. Toolchain and build prerequisites

### 8.1 Compiler

Use the maintained `awemorris/gcc-i386` source and record its exact commit.
Before building glibc, prove:

```sh
i386-linux-gnu-gcc -march=i386 -mtune=i386 -O2 ...
```

produces a trivial program with no post-386 instruction.

The initial compiler/runtime must provide:

- C11 support required by glibc 2.41;
- TLS code generation for the GNU i386 ABI;
- no unconditional SSE stack/code assumptions;
- a working 32-bit libgcc;
- no hidden `libatomic` dependency in glibc.

The compiler's generic `__atomic` CAS for `-march=i386` may call libatomic.
That is acceptable for ordinary packages later, but glibc itself must not
call libatomic. The future Debian archive will also need the same kernel
atomic UAPI implemented in GCC's libatomic, and possibly in libstdc++ atomic
fallbacks. Treat that as a post-glibc work item, not a reason to hide a glibc
dependency.

### 8.2 Kernel headers

Install headers generated from the same Linux 7.1 i386 tree that implements
the atomic syscall. Configure must fail when built against ordinary Debian
headers which lack the UAPI.

### 8.3 Build flags

First upstream-style build:

```text
CFLAGS="-O2 -g -march=i386 -mtune=i386"
CC="i386-linux-gnu-gcc"
--host=i386-linux-gnu
--build=x86_64-linux-gnu
--prefix=/usr
--disable-multi-arch
--enable-kernel=7.1.0
--enable-i386-kernel-atomics
```

Do not use `-Os` for the correctness bring-up. Size optimization can be
measured later. Do not use `-march=i486` to make configure pass.

The hard-float ABI is retained. Do not add `-msoft-float`.

### 8.4 Canonical upstream-style build command

`scripts/build-glibc-i386.sh` should implement this sequence without writing
inside the source tree:

```sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src="$repo/toolchain/glibc"
build="$repo/build/glibc-2.41-i386"
sysroot="$repo/build/sysroot-i386"
dest="$repo/build/glibc-2.41-i386-install"
cross="$repo/build/toolchain-i386/bin/i386-linux-gnu-"

test -f "$sysroot/usr/include/asm/unistd.h"
test -x "${cross}gcc"

mkdir -p "$build" "$dest"
cd "$build"

CC="${cross}gcc" \
CXX="${cross}g++" \
AR="${cross}ar" \
AS="${cross}as" \
LD="${cross}ld" \
NM="${cross}nm" \
OBJCOPY="${cross}objcopy" \
OBJDUMP="${cross}objdump" \
RANLIB="${cross}ranlib" \
READELF="${cross}readelf" \
STRIP="${cross}strip" \
CFLAGS="-O2 -g -march=i386 -mtune=i386" \
"$src/configure" \
    --build="$(gcc -dumpmachine)" \
    --host=i386-linux-gnu \
    --prefix=/usr \
    --with-headers="$sysroot/usr/include" \
    --enable-kernel=7.1.0 \
    --disable-multi-arch \
    --enable-i386-kernel-atomics

make -j32
make install install_root="$dest"
```

The script must refuse to reuse a build directory when its recorded source,
compiler, flags, kernel-header hash, or configure arguments differ. Save
`config.log`, `config.make`, compiler versions, environment, and the full
build transcript.

Do not run target test executables directly on the amd64 build host.
`scripts/run-glibc-tests-pc98.sh` supplies the full-system guest wrapper in
Phase 7. A separate native i686 control build may run `make check` locally.

Do not assume that `--sysroot` is automatically added by the cross compiler.
The toolchain build must define its sysroot consistently, or the script must
add the appropriate compiler option after verifying it does not make glibc
pick host headers.

### 8.5 Canonical Debian source materialization

`scripts/fetch-glibc-2.41-debian.sh` should:

```text
1. create a new versioned download directory;
2. download exactly the three locked source-package files;
3. verify SHA-256 before extraction;
4. run dpkg-source -x with an explicit destination;
5. save dpkg-source --before-build output and quilt series;
6. apply only the exported genuine-i386 patch queue;
7. print the final tree hash/manifest;
8. never delete or overwrite an existing extracted tree.
```

## 9. Implementation phases and gates

Every phase ends with a saved log under `artifacts/glibc-i386/<phase>/`.
Do not proceed when a gate fails.

### Phase 0: freeze and import references

Tasks:

1. create all source lock files;
2. import the Woody source into the historical glibc-i386 branch;
3. tag upstream 2.17, 2.41, and 2.43 references;
4. preserve the old Debian i486 instruction emulator patch with provenance;
5. materialize Debian `2.41-12+deb13u3` with `dpkg-source -x`;
6. produce a manifest of every applied Debian quilt patch.

Gate:

- all hashes reproduce;
- target and historical trees can be checked out independently;
- main worktree is on upstream 2.41 before functional changes.

### Phase 1: control builds

Tasks:

1. build unmodified Debian glibc 2.41 for its normal i686 target;
2. run its smoke tests on an i686 QEMU control;
3. attempt an unmodified `-march=i386` configure and archive the expected
   rejection;
4. compile/disassemble microtests for each intended i386 atomic instruction.

Gate:

- normal control build works;
- failure of the unmodified i386 build is exactly understood;
- toolchain can emit ordinary 386 code.

### Phase 2: kernel atomic UAPI

Tasks:

1. add UAPI/config/syscall;
2. implement safe user-memory atomic core;
3. patch futex operations for `CONFIG_M386`;
4. add and run self-tests;
5. boot existing musl BusyBox image unchanged.

Gate:

- kernel self-tests pass under `-cpu 386`;
- one million threaded and process-shared iterations pass;
- invalid address stress causes neither oops nor memory corruption;
- existing musl BusyBox still boots and its keyboard/disk/network tests pass.

### Phase 3: glibc config and sysdeps skeleton

Tasks:

1. add configure option and UAPI checks;
2. add `i386/i386` sysdeps selection;
3. add `MINIMUM_ISA 386`;
4. add explicit no-CPUID compile classification;
5. build only `csu/subdir_lib` if full build is not yet possible.

Gate:

- configure reports the intended sysdeps order;
- generated compiler command lines contain `-march=i386`;
- no i486/i586/i686 directory is selected as the baseline;
- configure still rejects a genuine-i386 build without the kernel option.

### Phase 4: glibc atomic backend

Tasks:

1. add minimal non-compiler-builtin backend;
2. add XCHG spin trylock;
3. build `ld.so`, libc, libpthread compatibility objects, libdl
   compatibility objects, and static libc;
4. inspect all undefined symbols.

Gate:

- no unresolved `__atomic_*`, `__sync_*`, or libatomic symbol;
- no post-386 opcode in mandatory executable code;
- symbol/ABI lists are unchanged;
- ordinary i486/i686 control builds are unchanged.

### Phase 5: loader and single-thread smoke tests

Use the existing musl BusyBox image as init/recovery. It does not use glibc,
which makes it a reliable harness. Copy the new glibc under
`/opt/glibc-i386` and run programs explicitly through its loader:

```sh
/opt/glibc-i386/lib/ld-linux.so.2 \
  --library-path /opt/glibc-i386/lib:/opt/glibc-i386/usr/lib \
  /opt/glibc-i386/tests/hello-dynamic
```

Tests:

- static and dynamic hello;
- argv, environ, auxv, errno;
- malloc/calloc/realloc/free;
- files, directories, stat, mmap, mprotect;
- fork/exec/wait;
- signals and alternate signal stack;
- `dlopen`, `dlsym`, `dlclose`, TLS in a DSO;
- locale `C` first, then generated UTF-8 locale;
- resolver and NSS files backend;
- time32/time64 and clocks;
- stdio and wide-character I/O.

Gate:

- dynamic hello and `dlopen` work under `-cpu 386`;
- no SIGILL, loader recursion, or early relocation failure;
- `LD_DEBUG=libs,reloc` completes;
- memory maps use the expected loader and DSOs.

### Phase 6: TLS and NPTL tests

Add focused tests with short failure timeouts:

| Area | Required cases |
|---|---|
| TLS | initial-exec, local-exec, general-dynamic, TLS in dlopened DSO, destructor |
| threads | create/join/detach, 1/2/32 threads, stack attributes |
| mutex | normal, recursive, error-check, adaptive if exposed, timed |
| robust | owner death, `pthread_mutex_consistent`, fork/shared mapping |
| process-shared | mutex, condvar, rwlock, semaphore in `MAP_SHARED` |
| condition variable | signal, broadcast, timeout, cancellation |
| rwlock | reader pressure, writer pressure, try/timed calls |
| semaphore | thread-shared and process-shared, post overflow |
| barrier | reuse for at least 100,000 generations |
| once | concurrent success and cancellation |
| cancellation | deferred and asynchronous test cases already used by glibc |
| fork | atfork handlers while other threads allocate/dlopen |
| spin | lock, trylock, unlock under contention |

Run each stress test for at least one million state transitions in QEMU.
Use deterministic seeds and save them.

Gate:

- zero lost wakeups and zero deadlocks;
- process-shared and robust tests pass;
- futex paths execute without SIGILL;
- no public pthread object size or alignment changed.

### Phase 7: upstream glibc tests

Run:

```sh
make -k check
```

The authoritative test run is full-system qemu-pc98, not qemu-user, because
the atomic syscall is supplied by the guest kernel.

For remote testing, use glibc's supported wrapper:

```sh
make check \
  test-wrapper="/path/to/glibc/scripts/cross-test-ssh.sh guest-hostname"
```

The source and build directories must be visible at the same absolute paths
on build host and guest. Use NFS over LGY-98 or an equivalent shared path.
Keep the musl-linked Dropbear/SSH recovery path independent of glibc.

Test order:

1. `csu`, `elf`, `dlfcn`;
2. `stdlib`, `malloc`, `string`, `stdio-common`;
3. `nptl`;
4. `rt`, `resolv`, `nss`;
5. locale, iconv, math;
6. full suite.

Gate:

- zero unexplained failures in `elf`, `dlfcn`, `malloc`, and `nptl`;
- every skipped or expected failure has a written reason;
- repeat the complete run at least twice;
- compare with the i686 control result so host/environment failures are not
  mislabeled as i386 failures.

### Phase 8: Debian glibc package

Add a local build profile, for example:

```text
legacy386
```

Use it together with:

```text
DEB_BUILD_PROFILES="legacy386 nobiarch"
```

Packaging changes:

- append `+pc98i3861` to the Debian version;
- set genuine-i386 C/C++ flags;
- pass `--disable-multi-arch`;
- pass `--enable-i386-kernel-atomics`;
- suppress amd64 and x32 alternate-library passes with `nobiarch`;
- retain Debian's existing `i386` ABI patches;
- retain Debian's existing symbols and package names;
- add a strict pre-dependency or documented kernel minimum for the atomic
  UAPI;
- do not upload to or mix with Debian's official binary-i386 suite.

Run both:

```text
stage1 -> headers/start files
stage2 -> libc with reduced dependencies
normal -> complete packages and tests
```

Gate:

- `.deb` packages build reproducibly;
- package contents and maintainer scripts match Debian expectations;
- install, upgrade, and remove work in a disposable rootfs;
- dynamic BusyBox linked to this glibc starts as `/sbin/init`;
- `ldconfig`, `getconf`, `getent`, `locale`, and `iconv` run.

### Phase 9: Debian 13 bootstrap

Use a private suite name such as:

```text
trixie-i386-legacy
```

Rules:

- never configure Debian's official binary-i386 repository in that rootfs;
- reuse `Architecture: all` packages;
- rebuild every `Architecture: any` package;
- publish `Sources`, `Packages`, `Release`, and signed metadata;
- record source version, binary version, build logs, toolchain hash, and
  build environment for each package.

Suggested bootstrap order:

1. kernel headers and binutils;
2. bootstrap GCC without target libc;
3. glibc headers and start files;
4. libgcc;
5. full glibc;
6. final GCC and libstdc++;
7. `dpkg`, `make`, shell, coreutils, findutils, grep, sed, tar, xz;
8. apt and its dependency closure;
9. build-essential;
10. sbuild/buildd and the remainder of Essential/Required.

Use Debian rebootstrap methods where possible. Store local deltas as a
machine-readable patch queue rather than editing unpacked packages manually.

Gate:

- a clean chroot can install only from the private archive;
- `dpkg --audit` is clean;
- an Essential package can rebuild itself inside the chroot;
- package dependency metadata contains no official SSE2 i386 binary.

### Phase 10: autobuild network

Only after Phase 9:

- deploy buildd/sbuild workers;
- make the qemu-pc98 worker a correctness reference, not the high-throughput
  builder;
- use faster i686/amd64 hosts for cross-builds only when the final test runs
  under the i386 kernel;
- sign repository metadata;
- retain failed logs and reproducible build inputs;
- add a package-level opcode scanner before accepting a binary.

The glibc milestone is not “all Debian packages build.” It is that glibc is
correct enough to become the trusted base of that later build network.

## 10. Opcode and binary audit

Create `scripts/check-i386-opcodes.py`. It must examine executable sections,
not raw byte strings.

Reject mandatory baseline occurrences of at least:

```text
cmpxchg cmpxchg8b xadd bswap cpuid rdtsc rdpmc
cmov* fcmov*
fxsave fxrstor
mm0..mm7 xmm* ymm* zmm*
```

Also reject unexpected GNU property requirements and compiler runtime calls.

Required checks:

```sh
readelf -hW
readelf -lW
readelf -nW
readelf -sW
readelf -rW
objdump -drwC
```

Special rules:

- `int $0x80`, `%fs`, `%gs`, x87, and memory `XCHG` are allowed;
- `LOCK` with an instruction available on 80386 is allowed;
- x87 is allowed under the hard-float target assumption;
- data bytes that decode as an instruction are not a failure;
- if glibc CPU multiarch is later enabled, optional implementation sections
  are reported separately and runtime dispatch is tested independently.

Scan:

- `ld-linux.so.2`;
- `libc.so.6`;
- all glibc DSOs;
- static archives;
- startup objects;
- every smoke-test executable;
- eventually every private Debian package.

## 11. ABI validation

Before and after each functional patch:

1. compare every `*.abilist`;
2. compare `readelf --dyn-syms --version-info`;
3. compare public header preprocessing output;
4. compare sizes, alignments, and field offsets for:
   `pthread_mutex_t`, `pthread_cond_t`, `pthread_rwlock_t`,
   `pthread_barrier_t`, `sem_t`, `ucontext_t`, `jmp_buf`, and TCB fields;
5. run old Debian i386 binaries built for an i486 only when they contain no
   forbidden instruction, to separate ABI from ISA compatibility;
6. run a program built against the new headers with the normal i686 glibc and
   vice versa where symbol versions permit.

There must be no new public `GLIBC_*` symbol version solely for this port.
The kernel atomic service is an implementation dependency, not a libc API.

## 12. Test root filesystem design

Keep three independent payloads:

```text
/bin/busybox-musl-static        recovery init and shell
/bin/dropbear-musl-static       remote test/control path
/opt/glibc-i386/                glibc under test
```

Then add:

```text
/opt/glibc-i386/tests/
/opt/glibc-i386/logs/
/opt/glibc-i386/lib/ld-linux.so.2
```

Do not replace the recovery init with glibc until the loader, malloc, TLS,
and pthread gates pass. This makes a broken libc debuggable.

For the first tests use at least 64 MiB RAM. Low-memory optimization belongs
after correctness. A final 80386 PC-98 profile can then measure 32, 16, 8,
and 4.6 MiB separately.

## 13. Failure triage

### Configure fails on CAS probe

- confirm `config_machine` is exactly `i386`;
- confirm `--enable-i386-kernel-atomics`;
- confirm the patched kernel headers are first in the sysroot;
- do not add `-march=i486`.

### Link has `__atomic_*` or `__sync_*`

- verify the genuine-i386 atomic header was selected;
- verify `USE_ATOMIC_COMPILER_BUILTINS == 0`;
- inspect preprocessed `include/atomic.h`;
- do not link glibc against libatomic.

### SIGILL before `main`

- capture EIP and disassemble `ld.so`/libc;
- run opcode scanner;
- inspect CPUID classification and IFUNC resolver;
- verify `pthread_spin_trylock` override;
- do not “fix” it by changing QEMU's 386 model.

### Dynamic loader hangs

- use `LD_DEBUG=libs,reloc`;
- test a no-TLS, then TLS executable;
- inspect early atomic syscall inline assembly and `%ebx` preservation;
- verify the kernel syscall cannot sleep while holding its atomic lock.

### pthread deadlock

- run the smallest focused primitive test;
- trace futex syscalls;
- verify `arch/x86/include/asm/futex.h` has no executed `CMPXCHG`;
- separate private vs process-shared and robust vs non-robust cases;
- save deterministic seed and shared word state.

### Test works on i486 but not 386

- compare opcode scans;
- compare `AT_HWCAP`, CPUID feature state, and IFUNC choice;
- verify the same glibc files and kernel UAPI version;
- treat the i486 result only as a control, not success.

### Works in qemu-user but not qemu-pc98

- qemu-user is not authoritative and may not implement the custom syscall;
- use full-system qemu-pc98 and the guest kernel;
- do not add a qemu-user-only workaround to glibc.

## 14. Risk register

| Risk | Likelihood | Impact | Mitigation / stop condition |
|---|---:|---:|---|
| kernel user-memory atomic service has a fault race or security flaw | medium | critical | prefault, in-atomic access, strict UAPI, hostile self-tests; stop before glibc if any oops/corruption |
| futex retains hidden `CMPXCHG` | high | high | explicit `futex.h` audit and robust/pshared tests |
| compiler emits post-386 instructions outside atomic code | medium | high | executable-section scanner on every artifact |
| no-CPUID build is misclassified as i686 | high before patch | high | explicit `__i386__` branch and loader feature tests |
| early `ld.so` atomic call corrupts PIC `%ebx` | medium | high | inline asm review plus earliest loader smoke test |
| process-shared pthread semantics are accidentally weakened | medium | critical | kernel-global serialization and fork/shared stress tests |
| syscall atomic overhead makes threaded applications unusably slow | high | medium | correctness first; profile, then optimize native operations or selected NPTL paths |
| glibc tests time out merely because an emulated 386 is slow | high | medium | per-test timing baselines, longer timeout, fast control runs, no unexplained skips |
| historical Woody code is copied as if it were NPTL | medium | high | historical branch is reference-only and never merged |
| old Debian invalid-opcode emulator reintroduces a known class of security bug | medium | critical | do not apply it; syscall first |
| hard-float binaries fail on a physical 386 without 387 | medium | high for those machines | clearly require 387/emulation; soft-float is a separate ABI project |
| Debian official i386/SSE2 binary contaminates private rootfs | high | critical | isolated suite, no official binary-i386 source, package provenance scanner |
| `2.41-12+deb13u4` or later security update changes target | high over time | medium | source lock and scheduled rebase; never silently download latest |
| Debian package scripts assume amd64/x32 biarch outputs | medium | medium | `nobiarch` first; inspect package file lists |
| symbol/version/layout drift | low if plan followed | critical | ABI gate before tests and packages |
| custom syscall is hard to upstream | high | medium | keep UAPI generic x86/i386, PC-98-independent; later evaluate NPTL TAS or safe #UD optimization |
| libatomic/libstdc++ still need i486 atomics | high | high for full Debian | share the kernel UAPI in later GCC/libatomic phase; glibc milestone remains independent |
| low-memory PC-98 cannot run full glibc/Debian | high | medium | use ample RAM for correctness, measure and optimize afterward |

## 15. Rejected shortcuts

Do not:

- build with `-march=i486` and label it i386;
- change QEMU `-cpu 386` to accept i486 instructions silently;
- disable pthread support or ship stub pthread functions;
- use a process-local global lock for process-shared atomics;
- alter pthread public layouts to add hidden lock words;
- link glibc or `ld.so` to libatomic;
- mix Debian 13 official i386 binaries into the private suite;
- copy LinuxThreads code into NPTL without a new design;
- run only hello-world and declare success;
- skip opcode scanning because QEMU happened to boot;
- apply the old Debian instruction-emulation patch without a security review.

## 16. Instructions for the implementation agent

The implementation agent should execute one phase at a time.

For every phase:

1. read this entire document;
2. record current main repository and all submodule commit IDs;
3. require a clean worktree or identify user-owned changes;
4. perform only the phase's listed files and tasks;
5. build with logged commands;
6. run the phase gate;
7. write `artifacts/glibc-i386/<phase>/RESULT.md` containing:
   - exact commits;
   - exact commands;
   - pass/fail counts;
   - unresolved questions;
   - binary hashes;
8. stop on a gate failure;
9. do not commit unless the user has reviewed the diff;
10. never push or release unless explicitly authorized for that step.

If a source change outside the file table becomes necessary, stop and append:

```text
Observed failure:
Evidence:
Why planned abstraction is insufficient:
Proposed extra file/function:
ABI/security effect:
Smallest validating test:
```

Then request design review. Do not grow an unexplained patch set.

## 17. Definition of glibc port success

The glibc 2.41 port is complete only when all are true:

- exact Debian `2.41-12+deb13u3` (or deliberately updated locked stable
  version) is used;
- qemu-pc98 starts with `-M pc9801 -cpu 386`;
- Linux 7.1 i386 boots without executing a post-386 instruction;
- `/lib/ld-linux.so.2` loads a dynamic glibc executable;
- a glibc-linked BusyBox can run as `/sbin/init` with display and keyboard;
- malloc, loader, TLS, pthread, robust and process-shared tests pass;
- glibc's full test suite has zero unexplained core/NPTL/loader failures;
- executable-section scanning finds no forbidden baseline opcode;
- there are no unresolved libatomic/compiler atomic helpers;
- public ABI and symbol versions match Debian i386 glibc 2.41;
- Debian libc packages install and upgrade in a disposable private-suite
  rootfs;
- source locks, patch exports, build logs, and test logs are reproducible.

The later Debian archive goal is complete only when a clean
`trixie-i386-legacy` chroot can rebuild an Essential package using no official
SSE2 i386 binary. That is deliberately beyond the glibc implementation
milestone.

## 18. Final review gates before implementation starts

The human reviewer should explicitly approve these decisions:

1. target baseline is Debian `2.41-12+deb13u3`, subject to a stable-update
   recheck;
2. Woody `2.2.5-11.8` is stored as a historical branch, not merged;
3. first correctness path uses a new kernel atomic syscall;
4. full invalid-opcode emulation is not on the critical path;
5. initial glibc build disables CPU multiarch;
6. hard-float/80387-compatible execution is the initial ABI;
7. private Debian suite retains architecture name `i386` but never mixes
   official binary-i386 packages;
8. low-memory optimization begins only after correctness.

Once these are approved, Phase 0 is mechanical and can be assigned to a
lower-capability implementation model. The first non-mechanical review point
is the kernel user-memory atomic service in Phase 2.

## 19. Primary references

- Debian 13 libc6 package:
  `https://packages.debian.org/trixie/libc6`
- Debian 13 i386 limitations:
  `https://www.debian.org/releases/trixie/release-notes/issues.html#reduced-support-for-i386`
- Debian Woody glibc source:
  `https://sources.debian.org/src/glibc/2.2.5-11.8/`
- Debian archive glibc pool:
  `https://archive.debian.org/debian/pool/main/g/glibc/`
- Debian 2005 GNU triplet change:
  `https://lists.debian.org/debian-devel-announce/2005/06/msg00010.html`
- Debian historical real-i386 discussion:
  `https://lists.debian.org/debian-devel/2005/10/msg00388.html`
- Debian historical i486 instruction-emulation discussion:
  `https://lists.debian.org/debian-release/2005/04/msg00057.html`
- glibc configure and cross-test instructions:
  `https://sourceware.org/glibc/manual/latest/html_node/Configuring-and-compiling.html`
- Debian new port procedure:
  `https://wiki.debian.org/PortsDocs/New`
- Debian buildd:
  `https://www.debian.org/devel/buildd/`

## 20. Second-pass failure analysis

This section deliberately assumes that the design above is wrong in at least
one important way.  It separates risks that can invalidate the architecture
from ordinary implementation defects.  Passing a small dynamically linked
program is necessary, but it is not sufficient evidence that glibc is usable
as the base of a Debian archive.

### 20.1 Risk review matrix

| Risk | Likely failure signature | Prevention and detection | Fallback or decision point |
|---|---|---|---|
| The private atomic syscall cannot return both an old 32-bit value and an error unambiguously | A valid value whose bit pattern resembles `-errno` is treated as failure, or a bad user pointer appears to succeed | Prototype the UAPI before freezing it. Prefer a pointer to a versioned result/request structure if raw `EAX` cannot represent both domains. Test every boundary bit pattern and invalid pointer. Do not publish the syscall ABI until this gate passes | Change the private UAPI while it is still experimental; never preserve an unsafe return convention for compatibility |
| Syscall-backed atomics are correct but too slow for the dynamic loader, malloc or NPTL | Boot appears hung; syscall trace is dominated by the atomic service; time increases dramatically with threads | Add per-operation kernel counters and a tracepoint during development. Establish a native i486 control and record loader, malloc and pthread timing plus operation counts. Implement i386-native `xchg`/`lock` operations before routing anything through the syscall | Replace hot operations with audited i386 test-and-set locks; reserve the syscall for the operations that cannot be expressed safely without `cmpxchg` |
| A user-memory atomic faults, sleeps or observes a COW mapping change while the kernel is manipulating it | Deadlock, double update, corrupted word, or a result that depends on whether the page was already resident | Define precisely whether the service may fault. Use the architecture's guarded user-access primitives, keep preemption/interrupt rules explicit, and do not hold an internal spinlock across a fault. Test anonymous COW after `fork`, file mappings, unmapped pages, read-only pages and mapping churn | Narrow the service to aligned private writable words if a general UAPI cannot be made safe. Reconsider a small, reviewed #UD path only if the narrowed service is insufficient |
| A signal arrives around an atomic syscall and libc restarts or reports it incorrectly | An operation is performed twice, returns `EINTR` after taking effect, or a mutex remains locked | Make the operation appear completed entirely before or entirely after signal delivery. The raw glibc wrapper must not acquire generic automatic-restart behaviour accidentally. Stress with interval timers, nested handlers and cancellation | Give the syscall explicit non-restart semantics and retry only before the operation has taken effect |
| The kernel itself still emits a post-386 instruction | Panic or reboot before userspace; failures vary by kernel configuration | Scan `vmlinux`, modules and early loader code, not only glibc. Boot the exact release configuration with QEMU instruction logging. Re-run this scan after every compiler or configuration change | Fix the kernel/compiler source first. Userspace work must not be used to conceal a kernel baseline violation |
| rseq assumes instructions or ordering unavailable on a 386 | First thread starts, then faults or hangs in scheduler/libc interaction | Audit `kernel/rseq.c`, x86 rseq helpers and glibc registration/critical-section code. Verify the actual glibc tunable or build control, then run once with rseq disabled to isolate the fault and again with it enabled | A temporary private build may disable rseq, but a Debian-ready result must either pass rseq tests or document and enforce the disablement consistently |
| vDSO or `AT_SYSINFO` selects SYSENTER or another unsupported entry path | Dynamic binaries SIGILL very early, often before useful diagnostics | Dump guest auxv and disassemble the mapped vDSO. Test a forced `int 0x80` path and the normal path. Verify kernel CPU-feature selection on a CPU without CPUID | Suppress the incompatible vDSO entry for the 386 machine, or make glibc select `int 0x80`; retain a separate i486/i686 fast path |
| CPU probing or an IFUNC resolver executes CPUID or a later instruction | Failure occurs inside `ld.so` before `main`; disabling one multiarch directory merely moves the crash | Keep initial multiarch disabled, audit all executable sections including TLS descriptor and resolver code, and test on a strict QEMU `-cpu 386`, not merely on a newer CPU with features masked incompletely | Add a no-CPUID baseline path. Re-enable optimized variants individually only after the base system is stable |
| TLS setup is subtly wrong on the PC-98 i386 context-switch path | Single-thread programs work, while signals or the second thread corrupt `%gs` data | Exercise static TLS, dynamic TLS, dlopen TLS, fork, signal delivery and rapid context switching. Inspect `set_thread_area`, GDT setup and `%gs` save/restore on the actual kernel | Keep TLS model changes isolated from atomic changes so the failing layer can be reverted independently |
| 64-bit libc counters still require CAS semantics | Link failures such as `__atomic_link_error`, or rare corruption in condition variables/time64 paths | Inventory every 64-bit atomic call and every compiler-generated `__atomic_*`/`__sync_*` reference. Link all glibc subdirectories and run wide-counter wraparound tests | Supply a lock-based i386 implementation in libc or the private syscall; do not silently use an i486 libatomic binary |
| Startup code calls the new helper before relocations, TLS or errno exist | Static programs work but shared/static-PIE binaries fail in `_dl_start`, or vice versa | Build and test shared, static and static-PIE matrices. Keep the early helper header-only/raw-syscall and independent of PLT, TLS, errno and cancellation | Split an early-loader implementation from the normal libc wrapper if their constraints differ |
| The private syscall number collides with a future kernel change or is blocked by seccomp/audit policy | `ENOSYS`/`EPERM` only in some rootfs configurations; a newer kernel silently invokes the wrong ABI | Give the feature an explicit kernel config and discoverable ABI/version, lock kernel and libc package dependencies, test under the intended seccomp policy, and reserve the number only in the private branch | Renumber before public release. Do not claim the resulting libc works with an unpatched stock kernel |
| Debian stable updates move the glibc baseline after patch development begins | Patches apply with offsets or tests differ from the documented source hash | Pin the exact Debian source/version for each release artifact. CI must reconstruct from the documented source package and exported patch set. Rebase stable security updates as reviewed changes | Publish a new coordinated kernel/libc revision; never rebuild an old binary name from a new unrecorded baseline |
| GCC, crt objects, libgcc or another sysroot component introduces i486 instructions | glibc itself scans clean but a trivial linked program SIGILLs | Scan all executable sections of the final sysroot and every linked dependency, including `crt*.o`, `libgcc*`, dynamic loader, NSS modules and test binaries. Record producer versions in `.buildinfo`-style metadata | Patch/rebuild the contaminating component from source for the 386 baseline; quarantine foreign binaries |
| Cross-test wrappers report skips as apparent success | `make check` looks mostly green while NPTL/loader tests never ran | Save full summaries, classify expected skips in a versioned allow-list, and fail the gate on new or unexplained skips. Repeat critical tests inside the booted guest without the wrapper | No release on cross-test results alone |
| QEMU hides real 386 timing, FPU, alignment or bus behaviour | All virtual tests pass, but a physical machine faults or hangs | Test at least one real 386 after loader/NPTL stabilization. Record exact CPU, 387 presence, RAM and board. Add high-rate signal and unaligned-boundary tests under both environments | Describe the release honestly as QEMU-only until hardware passes; keep hardware-specific fixes separate |
| The hard-float ABI is unusable on a common 386 without a 387 | Floating-point programs fault despite the integer userland working | Inventory the target machines and verify kernel math emulation or external 387 availability. Add a simple and an exception-heavy floating-point test to the hardware gate | Require 387/math emulation for the first release, or plan a separately named soft-float ABI; never mix the two repositories |
| glibc is functionally correct but too large for the intended RAM budget | Loader succeeds at 16/32 MiB but applications OOM on low-memory hardware | Measure proportional set size, dirty pages and peak boot memory separately from correctness. Strip locales/NSS features only through explicit packaging profiles after ABI tests pass | Keep low-memory libc/rootfs as a later profile. Do not destabilize the base port before correctness is established |
| Debian packages contain their own i486+ inline assembly or build-system assumptions | The base chroot works, but a significant fraction of Essential/build-essential fails with SIGILL or refuses the architecture | Add post-build opcode scanning and classify failures by source package. Maintain source-level port patches, never binary substitutions. Bootstrap by dependency tiers rather than alphabetical order | Mark packages `failed`/`needs-review`; do not allow one package failure to stop unrelated queue work |
| The project accidentally mixes official Debian i386 binaries, which are not a real-386 baseline | Tests pass until a less-used program or maintainer script executes SSE2/i686 code | Give the private suite and chroot an unmistakable name, record origin and baseline in package metadata, reject foreign binary origins at repository admission, and scan every upload | Rebuild the contaminated dependency from source; recreate the chroot if provenance cannot be proved |

### 20.2 Mandatory adversarial tests added to the plan

The implementation phases must add these tests before the result is called a
Debian-capable libc:

1. **Atomic UAPI test:** all old/new value bit patterns, alignment boundaries,
   invalid/read-only pointers, COW after `fork`, mapping removal attempts, and
   high-rate signals.
2. **Exactly-once test:** count successful operations while signals and thread
   cancellation are injected; no operation may be lost or applied twice.
3. **Early-process matrix:** shared, static and static-PIE hello-world programs,
   each with TLS and a signal handler, under a strict `-cpu 386`.
4. **Entry-path test:** normal vDSO selection and forced `int 0x80`, with guest
   auxv and vDSO disassembly archived.
5. **Thread/process matrix:** private, process-shared and robust mutexes;
   condition variables; owner death; fork from a multithreaded program; and
   dlopen-created TLS.
6. **Executable closure scan:** kernel, modules, loader, libc, crt objects,
   libgcc, NSS modules, the test executables and every package admitted to the
   bootstrap repository.
7. **Skip audit:** glibc test-suite skips are compared with a reviewed,
   versioned allow-list rather than counted as passes.
8. **Hardware gate:** after QEMU passes, execute loader, signal, mutex, TLS and
   floating-point smoke tests on a documented physical 386/387 configuration.

### 20.3 Architecture kill criteria and controlled fallbacks

The kernel-assisted design must be reviewed again, rather than patched around,
if any of these conditions is observed:

- the user-memory operation cannot be made fault-safe without sleeping while
  holding an atomicity lock;
- a signal can cause an operation to be applied twice or reported as failed
  after it was applied;
- the dynamic loader requires so many atomic syscalls that boot is not
  operationally usable on a real 386;
- robust/process-shared pthread primitives cannot survive owner death and
  process exit;
- the final sysroot cannot be kept free of undeclared post-386 instructions;
- the intended hardware has neither a 387 nor viable kernel math emulation,
  while the repository claims an ordinary hard-float `i386` ABI.

Fallbacks, in preferred order, are:

1. keep native i386 `xchg`/interrupt-safe lock operations for the hot paths and
   narrow the kernel service to the small set of true compare/exchange needs;
2. replace selected glibc algorithms with reviewed i386 lock-based algorithms,
   maintaining their public ABI and process-shared semantics;
3. consider a narrowly scoped invalid-opcode emulator only after a separate
   kernel security review and only for enumerated instruction forms;
4. label a single-process or QEMU-assisted result as a research profile, not as
   the Debian 13 i386 port.

These are decision branches, not permission to weaken the success criteria in
Section 17.

## 21. Debian package autobuild queue

This is adjacent to, but not part of, the initial glibc implementation.  It is
included because a real Debian userland cannot be maintained by asking an AI
agent or a human to build packages one at a time.

### 21.1 Use Debian's build engine, but stage the coordinator

Debian's established architecture is `wanna-build` plus `buildd`, with `sbuild`
performing the actual isolated package build.  That is the desirable mature
shape.  For an early private port, however, deploying and administering the
whole archive service creates work before the ABI is stable.  The staged plan
is therefore:

1. use **sbuild** as the only package build executor from the first prototype;
2. put a small, restartable coordinator in front of it for the private port;
3. keep queue states close to Debian `wanna-build` states so records can later
   be imported or mapped;
4. migrate to, or interoperate with, `wanna-build`/`buildd` when the port and
   archive policy are stable enough to justify it.

The custom component coordinates work; it must not reimplement Debian package
building, dependency resolution inside a chroot, or archive signing.

### 21.2 Required queue state model

Each immutable job key contains at least:

`source package + source version + suite + target profile + source snapshot +`
`toolchain revision + glibc ABI revision + kernel ABI revision`.

Changing any element creates a new job and marks obsolete unfinished jobs as
`superseded`; it must not silently reuse an old success result.

| State | Meaning |
|---|---|
| `pending` | Build dependencies are expected to be satisfiable and no worker owns the job |
| `blocked-deps` | Dependency information is incomplete or a required binary has not succeeded |
| `leased` | One worker owns a time-limited claim but has not yet started sbuild |
| `building` | sbuild is running and the worker is sending heartbeats |
| `succeeded` | Build, tests, provenance checks and opcode scan passed; artifacts remain quarantined |
| `failed-reproducible` | Repeated clean builds show a source/port failure requiring engineering |
| `failed-transient` | Worker, network, disk, mirror or timeout failure that may be retried |
| `needs-review` | Classification is uncertain, test policy failed, or an architecture patch is needed |
| `published` | A trusted publisher admitted and signed the verified artifacts |
| `superseded` | A newer source or ABI/toolchain generation replaced the job |

These map naturally to the important `wanna-build` ideas such as
`needs-build`, `building`, `uploaded`, `dep-wait`, `BD-Uninstallable`, `failed`
and `installed` without pretending the prototype already is Debian's service.

### 21.3 Lease and interruption semantics

- A worker claims a job with one atomic coordinator transaction and receives a
  lease ID plus expiry time.
- It sends heartbeats while sbuild is running.  Only that lease may update the
  job result.
- Ctrl-C or shutdown asks sbuild to terminate cleanly, uploads the partial log,
  and returns the job as transient.  If the worker disappears, lease expiry
  makes the job claimable again.
- The build is idempotent: no unique state exists only on the worker's disk.
  Repeating the same job may waste compute but must not corrupt queue state.
- Retry count and reason are retained.  Transient failures use bounded
  backoff; reproducible failures never spin indefinitely.
- A coordinator-side reaper handles expired leases.  Clocks are advisory;
  ownership is decided by the lease token and database transaction, not by a
  worker timestamp alone.

This makes stopping a volunteer worker an ordinary operation rather than a
recovery incident.

### 21.4 Distributed worker and trust design

The initial single-host implementation may use SQLite, but remote workers must
not open an SQLite database over NFS.  Distributed operation uses a small
authenticated HTTPS or SSH API in front of a single writer; PostgreSQL is the
natural next coordinator store if concurrency grows.

Workers advertise capability tags such as CPU baseline, available RAM/disk,
QEMU full-system support, native versus emulated execution, and trusted versus
untrusted operator.  The scheduler leases only eligible jobs.  A volunteer
worker receives source and a disposable sbuild environment; it does **not**
receive the archive signing key.

Every result uploads to quarantine with:

- complete stdout/stderr and sbuild log;
- source hashes and repository snapshot identifiers;
- `.buildinfo`, `.changes`, `.deb` and related artifacts;
- worker identity and capability declaration;
- kernel, glibc, compiler, binutils and build-script revisions;
- test summary, unexpected skips and executable opcode-scan result;
- elapsed time, resource use and failure classification.

Publishing is a serialized trusted operation after signature, provenance,
dependency, ABI and opcode checks.  Important bootstrap packages should be
rebuilt independently on a second trusted worker before publication.

### 21.5 Scheduling and bootstrap order

The queue must be dependency-aware, not FIFO.  It imports Debian source and
binary indices, calculates Build-Depends readiness, and first establishes a
reviewed bootstrap set:

1. kernel headers, binutils, GCC runtime/toolchain and libc;
2. dpkg, debhelper and Essential/build-essential closure;
3. sbuild/chroot and repository-management prerequisites;
4. packages needed by the selected minimal Debian rootfs;
5. the remaining archive in dependency-ready waves.

Dependency cycles require explicit bootstrap profiles or temporary staged
packages, each documented and later rebuilt normally.  A binary from official
Debian i386 must never satisfy a dependency silently, because its CPU baseline
is different.

### 21.6 User-operated commands

The first implementation should expose small non-interactive commands that are
safe to run from shell, screen or systemd:

- `queue-sync` imports source versions and dependency state;
- `queue-status` reports counts, active leases and actionable failures;
- `worker-once` claims and executes at most one eligible job;
- `worker-loop` repeats until stopped and treats Ctrl-C as normal;
- `queue-reap-leases` returns abandoned claims;
- `queue-retry` retries selected transient or reviewed failures;
- `queue-cancel` supersedes/cancels selected pending work;
- `publish` verifies and admits selected quarantined artifacts.

Routine operation is performed by the scripts and users' machines.  AI agents
and humans inspect only `needs-review`, improve patches or classification, and
return the package to the queue.  Logs and state, rather than an agent's chat
history, are the source of truth.

### 21.7 Queue implementation gates

Before multiple contributors are invited, demonstrate all of the following on
a disposable archive:

1. killing a worker mid-build and reclaiming its expired lease;
2. two workers attempting the same job without publishing duplicates;
3. invalidating a generation when glibc/kernel ABI revision changes;
4. preserving logs for transient and reproducible failures;
5. rejecting an artifact containing a forbidden instruction or foreign binary;
6. rebuilding one bootstrap package on two workers and comparing provenance;
7. operating workers without access to repository signing credentials;
8. reconstructing coordinator state from the database and artifact store after
   a host restart.

Only after these gates pass should unattended `worker-loop` operation be used
for a large Debian package set.

### 21.8 Additional autobuild references

- Debian autobuilder network:
  `https://www.debian.org/devel/buildd/`
- Debian wanna-build states:
  `https://www.debian.org/devel/buildd/wanna-build-states`
- Debian buildd operation:
  `https://www.debian.org/devel/buildd/operation.en.html`
- Debian Developer's Reference, buildd/wanna-build discussion:
  `https://www.debian.org/doc/manuals/developers-reference/ch05.en.html`
- Debian sbuild documentation:
  `https://wiki.debian.org/sbuild`
- Debian buildd setup notes:
  `https://wiki.debian.org/BuilddSetup`

## 22. Implementation record (2026-08-01 JST)

### 22.1 Result

The first working port described by this plan now exists. glibc 2.41 builds
with `-march=i386` and runs under qemu-pc98 with `-M pc9801 -cpu 386`. The
same source tree also builds and runs as an ordinary i486 libc, without
selecting the exact-i386 kernel-atomic sysdeps directory.

The runtime validation root uses a dynamically glibc-linked BusyBox as
`/sbin/init`; it is not a musl init which merely starts one glibc test
program. From that init, the test system successfully mounts filesystems,
enables swap, runs a shell script, loads the new dynamic linker and libc, and
executes the dedicated glibc and kernel-atomic tests. The final i386 display
ends with:

```text
1..12
ok 12 - unaligned request structure is rejected
I386_ATOMIC_SELFTEST_RC=0
GLIBC_I386_ALL_PASS
```

The corresponding i486 image, also using a dynamically glibc-linked BusyBox
as init, ends with `GLIBC_I486_ALL_PASS`.

This completes the executable proof-of-port milestone. It does **not** yet
satisfy every production criterion in section 17: the Debian patch stack,
the full upstream glibc test suite, ABI comparison, Debian binary packages,
and install/upgrade testing remain later gates.

### 22.2 Frozen source state

| Item | Implemented baseline |
|---|---|
| glibc source | upstream glibc 2.41, tag `upstream-2.41`, commit `88bdb2beb33cfd95defd85fcfebae4e54023c735`; port commit `cddb5b71c3ee05561cc78ed9c7f8f1e04f703d68` |
| glibc branch | `port/glibc-2.41-i386` in `toolchain/glibc` |
| kernel | this repository's Linux 7.1 PC-98 tree |
| compiler | repository GCC/Buildroot i386 cross compiler, invoked with `-march=i386 -mtune=i386` or `-march=i486 -mtune=i486` |
| runtime emulator | qemu-pc98, `-M pc9801 -cpu 386` and `-M pc9801 -cpu 486`, TCG |
| test memory | 32 MiB |

The glibc implementation is committed on `port/glibc-2.41-i386`. The exact
`git format-patch` export is stored under
`toolchain/patchsets/glibc/2.41/`; its replay from `upstream-2.41` must match
the submodule tree ID `be16c4a3a3d7876acbc4d9e569c833c7981689da`.

### 22.3 Kernel implementation actually used

The implemented interface is the versioned `i386_atomic` syscall numbered
472 in the PC-98 Linux i386 syscall table. Its UAPI is
`arch/x86/include/uapi/asm/i386_atomic.h`; version 1 supports naturally
aligned 8-, 16-, and 32-bit compare-and-exchange, exchange-and-add, exchange,
AND, OR, and XOR operations.

`CONFIG_X86_USER_ATOMIC_386` is selected only by the genuine `M386` kernel.
`M386` is uniprocessor-only, which is a correctness requirement of the
futex-side implementation. The i486 kernel includes only an `ENOSYS` syscall
stub and uses its native CPU atomics.

The syscall performs the following safety sequence:

1. copy and validate the versioned request;
2. reject invalid operations, widths, alignment, address ranges, and virtual
   overlap between request/result and target;
3. pin both target and result pages writable with `get_user_pages_fast`, so
   an i386 supervisor write cannot bypass a read-only PTE or COW;
4. map the pinned pages and perform the operation under a global raw spinlock
   with interrupts disabled;
5. publish the observed old value before publishing a target update;
6. dirty and release the pinned pages.

The result field is explicitly required to be naturally aligned. This
prevents a malicious request from placing the four-byte result across a page
boundary while only one result page is pinned.

The generic futex core cannot use an instruction sequence containing
`CMPXCHG` on a 386. The exact-i386 path in `arch/x86/include/asm/futex.h`
therefore calls `i386_user_atomic_op_inuser`. Because the futex core invokes
this path with page faults disabled, the helper uses
`get_user_pages_fast_only(FOLL_WRITE)`. A missing writable PTE returns
`-EFAULT`; the generic futex code faults it writable and retries. With the
UP-only constraint, a pinned PTE cannot be replaced by another CPU before the
raw-spinlocked access.

The port also corrected the existing exact-i386 `__copy_to_user_386` slow
path. It previously acquired `mmap_read_lock` and then called a GUP routine
which could acquire the same lock while resolving COW. That caused a real
boot-time deadlock. The fixed path uses `get_user_pages_fast(FOLL_WRITE)` one
page at a time and copies through the pinned kernel mapping without an outer
`mmap_read_lock`.

The minimal i386 and i486 configurations now explicitly enable `FUTEX` and
`COMPAT_32BIT_TIME`. The latter is required for the 32-bit futex syscall
entry; without it, syscall 240 resolved to the `ENOSYS` stub even when futex
support was otherwise enabled.

### 22.4 glibc implementation actually used

The port is intentionally opt-in. An exact i386 configure requires
`--enable-i386-kernel-atomics` and Linux headers containing ABI version 1 and
syscall 472. Configure rejects an exact i386 without that option, rejects the
option on i486 and newer targets, and prevents the special sysdeps leaf from
leaking into an i486 build.

The exact-i386 sysdeps leaf provides:

- a syscall-backed atomic machine for operations which need the observed old
  value or compare-and-exchange;
- native memory `XCHG` for lock exchange and native 386 locked arithmetic
  where no return value is required;
- an i386-safe spin trylock using memory `XCHG`;
- removal of the `PAUSE` (`REP NOP`) sequence from the exact-i386 spin loop;
- a CPUID-free CPU initialization path;
- the FNSAVE/FRSTOR TLS descriptor resolver rather than later FPU/SSE state
  instructions;
- minimum ISA reporting of 386;
- the new syscall number in `arch-syscall.h`.

The first build deliberately uses `--disable-multi-arch`. Opcode selection
by IFUNC and the Debian multiarch optimization directories remain outside the
first correctness milestone.

### 22.5 Reproducible commands added

From the repository root:

```sh
./build-glibc.sh i386
./build-glibc-tests.sh i386
./build-glibc-busybox.sh i386
./check-glibc-i386-opcodes.sh

./build-glibc.sh i486
./build-glibc-tests.sh i486
./build-glibc-busybox.sh i486
```

`GLIBC_BUILD_ROOT`, `I386_CROSS_PREFIX`, `BUSYBOX_SOURCE`,
`BUSYBOX_CONFIG`, and `JOBS` may be overridden. The scripts keep all build
objects and staging roots outside the source submodule. The BusyBox builder
copies Buildroot's already-configured BusyBox source before cleaning it, so it
does not damage the Buildroot work tree.

### 22.6 Runtime and adversarial validation matrix

| Test | i386 result | i486 result |
|---|---:|---:|
| `/lib/ld-linux.so.2` starts dynamic executable | pass | pass |
| dynamically glibc-linked BusyBox runs as `/sbin/init` | pass | pass |
| malloc/free | pass | pass |
| `dlopen`/`dlsym` of libm | pass | pass |
| TLS with three pthreads | pass | pass |
| mutex serialization, 3 x 200 operations | pass | pass |
| `fork` | pass | pass |
| process-shared robust mutex owner death | pass | pass |
| kernel atomic ABI probe | pass | not applicable (`ENOSYS` by design) |
| 8/16/32-bit atomic operations | pass | not applicable |
| compare mismatch is non-destructive | pass | not applicable |
| misaligned target rejected | pass | not applicable |
| inaccessible target fault-contained | pass | not applicable |
| read-only target remains unchanged | pass | not applicable |
| private mapping preserves fork COW | pass | not applicable |
| four-thread XADD serialization | pass | not applicable |
| process-shared XADD serialization | pass | not applicable |
| request/target virtual overlap rejected | pass | not applicable |
| unaligned request/result rejected | pass | not applicable |
| post-386 opcode and unresolved compiler-atomic scan of loader, DSOs, tests and BusyBox | pass | not required |

The dedicated atomic selftest is TAP-producing and currently has 12 tests.
The glibc smoke test additionally exercises the futex/robust-list path which
is distinct from the public atomic syscall path.

### 22.7 Recorded commit and patch export

The implementation is committed as
`cddb5b71c3ee05561cc78ed9c7f8f1e04f703d68` on
`port/glibc-2.41-i386`. The parent repository records the exact one-commit
series as
`toolchain/patchsets/glibc/2.41/0001-Add-Linux-assisted-atomics-for-genuine-i386.patch`.
The mechanical validator replays it from `upstream-2.41` and compares the
resulting tree ID with the submodule HEAD. This closes the reproducibility
step which was intentionally deferred until after source review.

### 22.8 Work which remains after the executable port

The following tasks are deliberately not claimed as complete:

- import and rebase the exact Debian 13 glibc packaging patch stack;
- run the full glibc testsuite and classify every failure/unsupported test;
- compare symbols, versions, structure layouts, and loader ABI against Debian
  13 i386 glibc;
- build `libc6`, `libc6-dev`, loader and related Debian packages;
- test install, upgrade, NSS, resolver, locale, iconv, time64 and cancellation
  behavior in a disposable root;
- test on physical 80386 PC-98 hardware;
- replace the 32 MiB validation setting with measured minimum-memory results;
- design and bootstrap the private Debian archive/buildd queue.

The kernel atomic ABI is coupled to this glibc build and is not proposed as
an upstream generic Linux ABI. Any future SMP-capable genuine-i386 kernel
must redesign or strengthen the futex helper; it must not simply remove the
current `M386`/UP restriction.
