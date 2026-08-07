#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
submodule_rel="third_party/noct"
submodule="$repo/$submodule_rel"

usage()
{
	cat <<'EOF'
Usage: scripts/update-noct.sh COMMAND [arguments]

Commands:
  init              initialize the pinned Noct submodule
  status            show the configured URL, pinned commit, and worktree state
  update [REF]      fetch origin and stage a new gitlink (default: origin/main)
  verify            verify gitlink, license, cleanliness, and tree hygiene
EOF
}

configured_url()
{
	git -C "$repo" config -f .gitmodules --get \
		"submodule.$submodule_rel.url"
}

is_initialized()
{
	test -e "$submodule/.git"
}

require_initialized()
{
	if ! is_initialized; then
		echo "Noct submodule is not initialized." >&2
		echo "Run: ./build.sh noct init" >&2
		exit 1
	fi
}

require_clean_submodule()
{
	require_initialized
	if test -n "$(git -C "$submodule" status --porcelain)"; then
		echo "Noct submodule worktree must be clean." >&2
		git -C "$submodule" status --short >&2
		exit 1
	fi
}

pinned_commit()
{
	git -C "$repo" ls-files -s "$submodule_rel" |
		awk '$1 == "160000" { print $2; exit }'
}

verify_submodule()
{
	local pinned actual generated

	require_clean_submodule
	test -f "$submodule/LICENSE" || {
		echo "Missing Noct zlib LICENSE" >&2
		exit 1
	}
	test -f "$submodule/CMakeLists.txt" || {
		echo "Incomplete Noct submodule" >&2
		exit 1
	}

	generated="$(find "$submodule" \
		\( -type d -name 'build-*' -o -type d -name CMakeFiles \
		-o -type f -name '*.o' -o -type f -name '*.a' \
		-o -type f -name '*.so' -o -type f -name CMakeCache.txt \) \
		-print -quit)"
	if test -n "$generated"; then
		echo "Generated file found in Noct submodule: $generated" >&2
		exit 1
	fi

	pinned="$(pinned_commit)"
	actual="$(git -C "$submodule" rev-parse HEAD)"
	if test -z "$pinned" || test "$actual" != "$pinned"; then
		echo "Noct submodule does not match the staged gitlink." >&2
		echo "pinned: ${pinned:-missing}" >&2
		echo "actual: $actual" >&2
		exit 1
	fi

	echo "Noct submodule verification: PASS"
	echo "Origin: $(configured_url)"
	echo "Commit: $actual"
}

command="${1:-status}"
shift || true
case "$command" in
	init)
		test "$#" -eq 0 || { usage >&2; exit 2; }
		git -C "$repo" submodule update --init --recursive -- "$submodule_rel"
		;;
	status)
		test "$#" -eq 0 || { usage >&2; exit 2; }
		echo "Origin: $(configured_url)"
		echo "Pinned: $(pinned_commit)"
		if is_initialized; then
			echo "Current: $(git -C "$submodule" rev-parse HEAD)"
			git -C "$submodule" status --short
		else
			echo "Current: not initialized"
		fi
		;;
	update)
		test "$#" -le 1 || { usage >&2; exit 2; }
		require_clean_submodule
		ref="${1:-origin/main}"
		git -C "$submodule" fetch --prune origin
		git -C "$submodule" checkout --detach "$ref"
		git -C "$repo" add "$submodule_rel"
		echo "Noct gitlink staged: $(git -C "$submodule" rev-parse HEAD)"
		;;
	verify)
		test "$#" -eq 0 || { usage >&2; exit 2; }
		verify_submodule
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
