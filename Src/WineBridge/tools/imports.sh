#!/bin/bash
# the functions Windows modules import from Orbiter.exe, one mangled name a line: tools/imports.sh <dll>... > api/imports.txt
for f in "$@"; do
	objdump -p "$f" | sed -n '/DLL Name: [Oo]rbiter\.exe/,/^$/p' | grep '<none>' | awk '{print $NF}'
done | sort -u
