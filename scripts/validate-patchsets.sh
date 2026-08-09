#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/linux-pc98-patchsets.XXXXXX")"
trap 'rm -rf -- "$tmp"' EXIT

validate()
{
	component="$1"
	version="$2"
	upstream_tag="$3"
	source="$repo/external/$component"
	patch_dir="$repo/external/patchsets/$component/$version"
	replay="$tmp/$component"

	if [ ! -e "$source/.git" ]; then
		echo "$component submodule is missing; run:" >&2
		echo "  git submodule update --init --recursive" >&2
		exit 1
	fi
	if ! compgen -G "$patch_dir/*.patch" >/dev/null; then
		echo "no patch exports found in $patch_dir" >&2
		exit 1
	fi

	git clone --quiet --shared --no-checkout "$source" "$replay"
	git -C "$replay" checkout --quiet "$upstream_tag"
	git -C "$replay" am --quiet "$patch_dir"/*.patch

	expected="$(git -C "$source" rev-parse 'HEAD^{tree}')"
	actual="$(git -C "$replay" rev-parse 'HEAD^{tree}')"
	if [ "$actual" != "$expected" ]; then
		echo "$component $version: patch replay differs from submodule HEAD" >&2
		echo "  replay tree:    $actual" >&2
		echo "  submodule tree: $expected" >&2
		exit 1
	fi
	printf '%-6s %-7s OK  %s\n' "$component" "$version" "$actual"
}

validate gcc 14.3.0 upstream-14.3.0
validate musl 1.2.6 upstream-1.2.6
# The glibc port branch re-imports the 2.41 sources on top of the 2.43-based
# main branch, so its baseline is that import commit.  Replace this with
# `upstream-2.41` once the glibc repository tags it.
validate glibc 2.41 88bdb2be
