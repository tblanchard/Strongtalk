#!/bin/zsh
# Usage: stcheck.sh [files...]  — syntax-check Strongtalk VM sources for arm64 macOS
cd ${0:A:h}/../Strongtalk/vm
INC=(); for d in */; do INC+=(-I$d); done
files=("$@"); [[ ${#files} -eq 0 ]] && files=(*/*.cpp)
mkdir -p /tmp/stlogs
for f in $files; do
  out=/tmp/stlogs/${f//\//_}.log
  nice -n 15 clang++ -arch arm64 -std=gnu++20 -fsyntax-only -ferror-limit=0 -Wno-everything -DDELTA_COMPILER -DASSERT -DDEBUG $INC $f >$out 2>&1
  print "$(grep -c 'error:' $out) $f"
done | sort -rn > /tmp/stlogs/summary.txt
awk '{s+=$1; if($1>0)n++} END{print "total errors:",s," files with errors:",n," of",NR}' /tmp/stlogs/summary.txt
