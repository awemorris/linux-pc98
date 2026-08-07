#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
vendor_rel="third_party/noct"
vendor="$repo/$vendor_rel"
metadata="$vendor/UPSTREAM.md"

usage()
{
	cat <<'EOF'
Usage: scripts/update-noct.sh COMMAND [arguments]

Commands:
  status            show the imported origin and commit
  import URL REF    initially import Noct with git subtree --squash
  update URL REF    update Noct with git subtree pull --squash
  verify            verify metadata, license, content hash, and tree hygiene
EOF
}

metadata_field()
{
	local name="$1"
	sed -n "s/^${name}: \`\([^\`]*\)\`.*/\1/p" "$metadata"
}

snapshot_hash()
{
	(
		cd "$vendor"
		find . -type f ! -name UPSTREAM.md -print0 |
			LC_ALL=C sort -z |
			xargs -0 sha256sum |
			sha256sum |
			awk '{print $1}'
	)
}

require_clean_repository()
{
	if test -n "$(git -C "$repo" status --porcelain)"; then
		echo "The linux-pc98 worktree must be clean for a subtree operation." >&2
		exit 1
	fi
}

resolve_ref()
{
	local url="$1" ref="$2"
	git -C "$repo" fetch --no-tags "$url" "$ref" >&2
	git -C "$repo" rev-parse FETCH_HEAD
}

write_metadata()
{
	local url="$1" commit="$2" hash
	hash="$(snapshot_hash)"
	{
		printf '# Noct upstream snapshot\n\n'
		printf 'Origin: `%s`\n' "$url"
		printf 'Commit: `%s`\n' "$commit"
		printf 'Snapshot content SHA-256: `%s`\n\n' "$hash"
		printf 'The snapshot is imported with `git subtree --squash`. '
		printf 'Normal builds are offline and never update this directory.\n'
	} > "$metadata"
}

verify_snapshot()
{
	local expected actual generated
	test -f "$metadata" || { echo "Missing $metadata" >&2; exit 1; }
	test -f "$vendor/LICENSE" || { echo "Missing Noct zlib LICENSE" >&2; exit 1; }
	test -f "$vendor/CMakeLists.txt" || { echo "Incomplete Noct snapshot" >&2; exit 1; }

	generated="$(find "$vendor" \
		\( -type d -name 'build-*' -o -type d -name CMakeFiles \
		-o -type f -name '*.o' -o -type f -name '*.a' \
		-o -type f -name '*.so' -o -type f -name CMakeCache.txt \) \
		-print -quit)"
	if test -n "$generated"; then
		echo "Generated file found in imported source: $generated" >&2
		exit 1
	fi

	expected="$(metadata_field 'Snapshot content SHA-256')"
	actual="$(snapshot_hash)"
	test -n "$expected" || { echo "Missing snapshot hash in UPSTREAM.md" >&2; exit 1; }
	if test "$actual" != "$expected"; then
		echo "Noct snapshot hash mismatch" >&2
		echo "expected: $expected" >&2
		echo "actual:   $actual" >&2
		exit 1
	fi

	echo "Noct snapshot verification: PASS"
	echo "Origin: $(metadata_field Origin)"
	echo "Commit: $(metadata_field Commit)"
}

command="${1:-status}"
shift || true
case "$command" in
	status)
		test -f "$metadata" || { echo "No Noct snapshot is registered." >&2; exit 1; }
		echo "Origin: $(metadata_field Origin)"
		echo "Commit: $(metadata_field Commit)"
		;;
	verify)
		verify_snapshot
		;;
	import)
		test "$#" -eq 2 || { usage >&2; exit 2; }
		test ! -e "$vendor" || { echo "$vendor_rel already exists" >&2; exit 1; }
		require_clean_repository
		resolved="$(resolve_ref "$1" "$2")"
		echo "+ git subtree add --prefix=$vendor_rel --squash $1 $resolved"
		git -C "$repo" subtree add --prefix="$vendor_rel" --squash "$1" "$resolved"
		write_metadata "$1" "$resolved"
		;;
	update)
		test "$#" -eq 2 || { usage >&2; exit 2; }
		test -d "$vendor" || { echo "$vendor_rel does not exist" >&2; exit 1; }
		require_clean_repository
		resolved="$(resolve_ref "$1" "$2")"
		echo "+ git subtree pull --prefix=$vendor_rel --squash $1 $resolved"
		git -C "$repo" subtree pull --prefix="$vendor_rel" --squash "$1" "$resolved"
		write_metadata "$1" "$resolved"
		;;
	-h | --help | help)
		usage
		;;
	*)
		echo "Unknown command: $command" >&2
		usage >&2
		exit 2
		;;
esac
