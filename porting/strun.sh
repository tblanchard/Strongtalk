#!/bin/zsh
# Build (porting/stlink.sh) and run the VM on ../strongtalk.bst under lldb; prints output and a backtrace on a crash.
/Users/todd/Projects/strongtalk-source-r36/Strongtalk/porting/stlink.sh
cd /Users/todd/Projects/strongtalk-source-r36
perl -e "alarm ${TIMEOUT:-120}; exec @ARGV" lldb --batch -o "run $*" -k 'bt 20' -k 'quit' /tmp/stobj/strongtalk 2>&1 \
  | grep -vE '^\(lldb\)|^Current executable|launched:|^---|LongView|Sun Micro|license|^Use and|^\s+[0-9]+\s' > /tmp/strun.txt
grep -v '^\s*$' /tmp/strun.txt | tail -${LINES_OUT:-45}
