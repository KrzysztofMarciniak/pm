#!/bin/sh
# Download license texts (SPDX license-list-data) and insert them into the pack
# as licenses/<ID> entries, with @@YEAR@@ / @@AUTHOR@@ placeholders.
# An entry that already exists is replaced in place, a new one is appended
# after the last licenses/* entry. Nothing is written if any download fails.
#
#   sh tools/add_licenses.sh [pack] [ID ...]
#
# No IDs = refresh every licenses/* entry already in the pack.
# LICENSE_URL=<base url> overrides the source (file:// works too).
set -u

PACK=${1:-templates/default.pack}
[ $# -gt 0 ] && shift
BASE=${LICENSE_URL:-https://raw.githubusercontent.com/spdx/license-list-data/main/text}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/pm-add.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

[ -f "$PACK" ] || { echo "no such pack: $PACK" >&2; exit 2; }
command -v curl >/dev/null 2>&1 || { echo "curl is required" >&2; exit 2; }

if [ $# -gt 0 ]; then
	IDS="$*"
else
	IDS=$(awk '/^%%% licenses\// { sub(/^%%% licenses\//, ""); print }' "$PACK")
fi
[ -n "$IDS" ] || { echo "no license IDs given and none in $PACK" >&2; exit 2; }

remote_name() {
	case $1 in
	GPL-3.0) echo deprecated_GPL-3.0 ;;
	*)       echo "$1" ;;
	esac
}

# no CR, no trailing blanks, no leading/trailing blank lines
clean() {
	tr -d '\r' | sed 's/[[:space:]]*$//' | awk '
		NF { on = 1 }
		on { buf[++n] = $0 }
		END { while (n > 0 && buf[n] == "") n--
		      for (i = 1; i <= n; i++) print buf[i] }'
}

# SPDX placeholders -> pm placeholders
subst() {
	sed -e 's/<year>/@@YEAR@@/g' \
	    -e 's/\[yyyy\]/@@YEAR@@/g' \
	    -e 's/<copyright holders>/@@AUTHOR@@/g' \
	    -e 's/<copyright holder>/@@AUTHOR@@/g' \
	    -e 's/<owner>/@@AUTHOR@@/g' \
	    -e 's/<name of author>/@@AUTHOR@@/g' \
	    -e 's/\[name of copyright owner\]/@@AUTHOR@@/g' \
	    -e 's/@@AUTHOR@@ \./@@AUTHOR@@./g'
}

mkdir "$TMP/new"
for id in $IDS; do
	url="$BASE/$(remote_name "$id").txt"
	if ! curl -fsSL --retry 2 --max-time 30 -o "$TMP/raw" "$url"; then
		echo "download failed: $url (pack left untouched)" >&2
		exit 1
	fi
	clean < "$TMP/raw" > "$TMP/body"
	[ -s "$TMP/body" ] || { echo "empty download: $url (pack left untouched)" >&2; exit 1; }

	hdr=
	body_sub=0
	case $id in
	Unlicense)
		;;
	Apache-2.0)
		hdr='Copyright @@YEAR@@ @@AUTHOR@@' ;;
	GPL-*|LGPL-*|AGPL-*|MPL-*|CC0-*)
		hdr=$(printf '@@NAME@@\nCopyright (C) @@YEAR@@ @@AUTHOR@@') ;;
	*)
		if grep -q -E '<year>|\[yyyy\]' "$TMP/body"; then
			body_sub=1
		else
			hdr='Copyright (c) @@YEAR@@ @@AUTHOR@@'
		fi ;;
	esac

	{
		printf '%s\n' "%%% licenses/$id"
		[ -n "$hdr" ] && printf '%s\n\n' "$hdr"
		if [ "$body_sub" -eq 1 ]; then subst < "$TMP/body"; else cat "$TMP/body"; fi
		printf '\n'
	} > "$TMP/new/$id"
done

updated= added=
for id in $IDS; do
	if grep -qx "%%% licenses/$id" "$PACK"; then updated="$updated $id"; else added="$added $id"; fi
done

awk -v ids="$IDS" -v dir="$TMP/new" '
	BEGIN { n = split(ids, arr, " "); for (i = 1; i <= n; i++) want[arr[i]] = 1 }
	function emit(id,    f, line) {
		f = dir "/" id
		while ((getline line < f) > 0) print line
		close(f); done[id] = 1
	}
	function flush_new(    i) {
		for (i = 1; i <= n; i++) if (!(arr[i] in done)) emit(arr[i])
		flushed = 1
	}
	/^%%% / {
		skipping = 0
		if (in_lic && $0 !~ /^%%% licenses\//) { flush_new(); in_lic = 0 }
		if ($0 ~ /^%%% licenses\//) {
			in_lic = 1
			id = substr($0, 14)
			if (id in want) { emit(id); skipping = 1; next }
		}
	}
	skipping { next }
	{ print }
	END { if (!flushed) flush_new() }
' "$PACK" > "$TMP/pack.new" || exit 1

cat "$TMP/pack.new" > "$PACK"
[ -n "$updated" ] && echo "updated:$updated"
[ -n "$added" ]   && echo "added:$added"
exit 0
