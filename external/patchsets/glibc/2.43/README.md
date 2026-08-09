# glibc 2.43 legacy-i386 patch set

glibc 2.43 is currently an unmodified research baseline.
`0001-Document-the-legacy-i386-port-baseline.patch` only adds the maintenance
document stored in the `glibc-i386` repository.

There is no claim that this source runs on an Intel 80386. Before adding a
functional patch, review and document at least:

- minimum CPU and instruction assumptions;
- atomic, TLS, pthread, and signal requirements;
- compiler runtime dependencies;
- generated and multiarch implementations;
- ABI and symbol-version compatibility.

Keep PC-98 hardware behavior out of glibc. A completed port should also be
usable by other legacy i386 systems such as FM TOWNS.
