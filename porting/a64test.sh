#!/bin/sh
# Checks the arm64 encoder (Strongtalk/vm/asm/a64.hpp) against the system assembler.
set -e
D=/tmp/a64t; mkdir -p $D
P=$(cd "$(dirname "$0")" && pwd)
clang++ -std=c++11 -I"$P/../Strongtalk/vm/asm" "$P/a64test.cpp" -o $D/gen
$D/gen > $D/expect.txt
cut -f2 $D/expect.txt | sed 's/^/  /' > $D/t.s
clang -c -arch arm64 $D/t.s -o $D/t.o
otool -t -X $D/t.o | awk '{for (i=2;i<=NF;i++) print $i}' > $D/words.txt
paste $D/words.txt $D/expect.txt | awk -F'\t' '$1 != $2 {bad++; printf "MISMATCH %s (assembler) != %s (a64)  %s\n", $1, $2, $3} END {printf "%d instructions, %d mismatches\n", NR, bad+0; exit bad > 0}'
