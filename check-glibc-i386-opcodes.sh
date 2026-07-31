#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
root="${GLIBC_BUILD_ROOT:-$repo/build/glibc-2.41}"
stage="$root/stage-i386"
tests="$root/tests-i386"
report="${OPCODE_REPORT:-$root/i386-opcode-scan.txt}"

if [ -n "${I386_CROSS_PREFIX:-}" ]; then
	cross_prefix="$I386_CROSS_PREFIX"
elif [ -x "$repo/build/release-v0.3.0/i386-buildroot/output/host/bin/i386-buildroot-linux-musl-objdump" ]; then
	cross_prefix="$repo/build/release-v0.3.0/i386-buildroot/output/host/bin/i386-buildroot-linux-musl"
else
	cross_prefix="$repo/build/i386-buildroot/output/host/bin/i386-buildroot-linux-musl"
fi
objdump="$cross_prefix-objdump"
readelf="$cross_prefix-readelf"
test -x "$objdump"
test -x "$readelf"

status=0
: >"$report"
for file in "$stage"/lib/*.so* "$tests"/* \
	"$root"/busybox-glibc-i386/busybox; do
	[ -f "$file" ] || continue
	problems=
	bad=$("$objdump" -d "$file" |
		awk -F '\t' 'NF >= 3 {
			asm = $3
			sub(/[[:space:]]*#.*/, "", asm)
			if (asm ~ /(^|[[:space:]])(lock[[:space:]]+)?(bswap|cmov[a-z]*|cmpxchg[0-9a-z]*|cpuid|rdtsc|rdmsr|wrmsr|rdpmc|rsm|xadd|pause|emms|femms|sysenter|sysexit|syscall|sysret|fxsave|fxrstor|xsave|xrstor|prefetch[a-z]*|[slm]fence|clflush[a-z]*|movnti|monitor|mwait|popcnt|lzcnt|tzcnt|movbe|rdrand|rdseed|adcx|adox|mulx|sh[lr]x|sarx|rorx|andn|bextr|bzhi|bls[imr]|pdep|pext|crc32)([[:space:]]|$)/)
				print
		}' | head -n 20 || true)
	if [ -n "$bad" ]; then
		problems="post-386 instructions:\n$bad"
	fi
	undefined=$("$readelf" -Ws "$file" |
		awk '$7 == "UND" && $8 ~ /^__(atomic|sync)_/ { print }' |
		head -n 20 || true)
	if [ -n "$undefined" ]; then
		problems="${problems}${problems:+\n}unresolved compiler atomic helpers:\n$undefined"
	fi
	if [ -z "$problems" ]; then
		printf 'OK  %s\n' "$file" >>"$report"
	else
		printf 'BAD %s\n%b\n' "$file" "$problems" >>"$report"
		status=1
	fi
done

cat "$report"
exit "$status"
