#!/bin/zsh
# Runs porting/results.st (filed in at quit, see STRONGTALK_EVAL_FILE in system_prims.cpp)
# interpreted and compiled (low invocation limit, $LIMIT, default 1000: most code runs compiled)
# and compares the printed results. Usage: compare_results.sh [vm binary] [benchmark run first]
VM=${1:-/tmp/stobj/strongtalk}
WARM=${2:-sieve}
LIMIT=${LIMIT:-1000}
cd /Users/todd/Projects/strongtalk-source-r36
export STRONGTALK_EVAL_FILE=/Users/todd/Projects/strongtalk-source-r36/Strongtalk/porting/results.st
filter() { grep -o 'RESULT .*' }
perl -e 'alarm 600; exec @ARGV' $VM -UseRecompilation $WARM 2>&1 | filter > /tmp/results_interpreted.txt
perl -e 'alarm 600; exec @ARGV' $VM +UseRecompilation +UseArm64Compiler InvocationCounterLimit=$LIMIT $WARM 2>&1 | filter > /tmp/results_compiled.txt
echo "interpreted: $(wc -l < /tmp/results_interpreted.txt) results, compiled: $(wc -l < /tmp/results_compiled.txt) results"
if diff /tmp/results_interpreted.txt /tmp/results_compiled.txt; then echo "results identical"; else echo "RESULTS DIFFER"; exit 1; fi
