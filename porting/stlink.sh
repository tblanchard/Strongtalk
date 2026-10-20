#!/bin/zsh
# Compile all VM sources to /tmp/stobj (debug, assertions on) and link /tmp/stobj/strongtalk.
# Undefined symbols are listed in /tmp/stobj/undefined.txt, compile errors in /tmp/stobj/compile_errors.txt.
# An object is rebuilt when its .cpp or any header is newer than it.
cd /Users/todd/Projects/strongtalk-source-r36/Strongtalk/Strongtalk/vm
INC=(); for d in */; do INC+=(-I$PWD/$d); done
mkdir -p /tmp/stobj
: > /tmp/stobj/compile_errors.txt
newest_hdr=$(ls -t */*.hpp | head -1)

jobs_running=0
for f in */*.cpp; do
  o=/tmp/stobj/${${f//\//_}%.cpp}.o
  if [[ $o -nt $f && $o -nt $newest_hdr ]]; then continue; fi
  ( nice -n 15 clang++ -arch arm64 -std=gnu++20 -c -g -O0 -w -DDELTA_COMPILER -DASSERT -DDEBUG $INC $f -o $o 2>>/tmp/stobj/compile_errors.txt ) &
  (( jobs_running++ ))
  if (( jobs_running >= 8 )); then wait; jobs_running=0; fi
done
wait

clang++ -arch arm64 /tmp/stobj/*.o -o /tmp/stobj/strongtalk 2>/tmp/stobj/link.txt
grep -E '^  "' /tmp/stobj/link.txt | sed -E 's/^  "([^"]*)".*/\1/' | sort -u > /tmp/stobj/undefined.txt
echo "objects: $(ls /tmp/stobj/*.o | wc -l)  compile errors: $(grep -c 'error:' /tmp/stobj/compile_errors.txt)  undefined symbols: $(wc -l < /tmp/stobj/undefined.txt)"
grep -q 'error' /tmp/stobj/link.txt && grep -v '^  "' /tmp/stobj/link.txt | head -5
exit 0
