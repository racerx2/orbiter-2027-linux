#!/bin/sh
# the symbols Orbiter (dynamic table) and the Orbitersdk library define, one a line, and that Orbiter's sha256 (WB_HELPER_FROM checks it)
# linuxsyms.sh <out> <Orbiter> <libOrbitersdk.a> <sha256 out>; the list is rewritten only when it changes, so the calls are made again only then
out=$1; shift
{ nm -D --defined-only --format=just-symbols "$1"; nm --defined-only --format=just-symbols "$2" 2>/dev/null; } | sort -u > "$out.new"
if cmp -s "$out.new" "$out"; then rm -f "$out.new"; else mv -f "$out.new" "$out"; fi
sha256sum "$1" | cut -d' ' -f1 > "$3"
