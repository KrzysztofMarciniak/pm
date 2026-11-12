#!/bin/sh
# Compare every licenses/* entry of a .pack against the canonical SPDX text
# downloaded from the net. Ignores case, whitespace, punctuation and the
# copyright/placeholder lines (those legitimately differ).
#
#   sh tests/test_licenses.sh [templates/default.pack]
#   LICENSE_URL=<base url> overrides the source (file:// works too).
set -u

PACK=${1:-templates/default.pack}
BASE=${LICENSE_URL:-https://raw.githubusercontent.com/spdx/license-list-data/main/text}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/pm-lic.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

[ -f "$PACK" ] || { echo "no such pack: $PACK" >&2; exit 2; }
command -v curl >/dev/null 2>&1 || { echo "curl is required" >&2; exit 2; }

# local text of one license from the pack
local_text() {
	awk -v want="%%% licenses/$1" '
		/^%%% / { on = ($0 == want); next }
		on' "$PACK"
}

# SPDX file name for a pack license id (deprecated ids live under deprecated_)
remote_name() {
	case $1 in
	GPL-3.0) echo deprecated_GPL-3.0 ;;
	*)       echo "$1" ;;
	esac
}

# Ignore what legitimately differs: case, punctuation, line breaks, http vs
# https, a leading "... License" title line, "All rights reserved." and the
# copyright/placeholder lines. Then one word per line.
normalize() {
	tr 'A-Z' 'a-z' < "$1" |
	sed -e 's#https://#http://#g' \
	    -e '/@@/d' \
	    -e '/<year>/d' \
	    -e '/\[yyyy\]/d' \
	    -e '/\[name of copyright owner\]/d' \
	    -e '/^[[:space:]]*all rights reserved\.\{0,1\}[[:space:]]*$/d' |
	awk 'NF && !done { done = 1; if ($0 ~ /license[ \t]*$/) next } { print }' |
	tr -cs 'a-z0-9' '\n' | sed '/^$/d'
}

IDS=$(awk '/^%%% licenses\// { sub(/^%%% licenses\//, ""); print }' "$PACK")
[ -n "$IDS" ] || { echo "no licenses/* entries in $PACK" >&2; exit 2; }

pass=0 fail=0
for id in $IDS; do
	url="$BASE/$(remote_name "$id").txt"
	if ! curl -fsSL --retry 2 --max-time 30 -o "$TMP/remote" "$url"; then
		echo "FAIL  $id  (download failed: $url)"
		fail=$((fail + 1))
		continue
	fi
	local_text "$id" > "$TMP/local"
	normalize "$TMP/local"  > "$TMP/a"
	normalize "$TMP/remote" > "$TMP/b"
	if [ ! -s "$TMP/b" ]; then
		echo "FAIL  $id  (downloaded file is empty)"
		fail=$((fail + 1))
	elif cmp -s "$TMP/a" "$TMP/b"; then
		echo "ok    $id"
		pass=$((pass + 1))
	else
		echo "FAIL  $id  (differs from $url)"
		if [ -n "${VERBOSE:-}" ]; then diff "$TMP/a" "$TMP/b"; else diff "$TMP/a" "$TMP/b" | head -n 10; fi | sed 's/^/        /'
		fail=$((fail + 1))
	fi
done

echo "licenses: $pass ok, $fail failed"
[ "$fail" -eq 0 ]
