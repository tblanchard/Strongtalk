#!/bin/zsh
# Compile all VM sources to /tmp/stobj (debug, assertions on) and link /tmp/stobj/strongtalk.
# Undefined symbols are listed in <dir>/undefined.txt, compile errors in <dir>/compile_errors.txt.
# An object is rebuilt when its .cpp or any header is newer than it.
# RELEASE=1: optimized build without assertions in /tmp/stobj_rel (develop flags stay settable).
cd ${0:A:h}/../Strongtalk/vm
INC=(); for d in */; do INC+=(-I$PWD/$d); done
if [[ -n $RELEASE ]]; then OUT=/tmp/stobj_rel; CFLAGS=(-O2 -g); else OUT=/tmp/stobj; CFLAGS=(-g -O0 -DASSERT -DDEBUG); fi
mkdir -p $OUT
: > $OUT/compile_errors.txt
newest_hdr=$(ls -t */*.hpp | head -1)

jobs_running=0
for f in */*.cpp; do
  o=$OUT/${${f//\//_}%.cpp}.o
  if [[ $o -nt $f && $o -nt $newest_hdr ]]; then continue; fi
  ( nice -n 15 clang++ -arch arm64 -std=gnu++20 -c $CFLAGS -w -DDELTA_COMPILER $INC $f -o $o 2>>$OUT/compile_errors.txt ) &
  (( jobs_running++ ))
  if (( jobs_running >= 8 )); then wait; jobs_running=0; fi
done
wait
for f in */*.mm(N); do			# Objective-C++ (the GUI, win32User.mm)
  o=$OUT/${${f//\//_}%.mm}_mm.o
  if [[ $o -nt $f && $o -nt $newest_hdr ]]; then continue; fi
  ( nice -n 15 clang++ -arch arm64 -std=gnu++20 -fobjc-arc -c $CFLAGS -w -DDELTA_COMPILER $INC $f -o $o 2>>$OUT/compile_errors.txt ) &
done
wait
for f in */*.S(N); do			# assembly sources (arm64 transitions)
  o=$OUT/${${f//\//_}%.S}_S.o
  if [[ $o -nt $f ]]; then continue; fi
  clang -arch arm64 -c $f -o $o 2>>$OUT/compile_errors.txt
done

clang++ -arch arm64 $OUT/*.o -o $OUT/strongtalk -framework AppKit -framework CoreGraphics -framework CoreText \
  -framework QuartzCore -framework ImageIO 2>$OUT/link.txt
grep -E '^  "' $OUT/link.txt | sed -E 's/^  "([^"]*)".*/\1/' | sort -u > $OUT/undefined.txt
echo "objects: $(ls $OUT/*.o | wc -l)  compile errors: $(grep -c 'error:' $OUT/compile_errors.txt)  undefined symbols: $(wc -l < $OUT/undefined.txt)"
grep -q 'error' $OUT/link.txt && grep -v '^  "' $OUT/link.txt | head -5
exit 0
