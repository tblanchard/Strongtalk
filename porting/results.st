"Expressions whose printed results are compared between interpreted and compiled runs
 (porting/compare_results.sh). Chunk format: every chunk ends with a bang; each result line
 starts with RESULT."!

Transcript show: 'RESULT factorial '; show: 100 factorial printString; cr!
Transcript show: 'RESULT smi max '; show: SmallInteger maxVal printString; show: ' '; show: SmallInteger minVal printString; cr!
Transcript show: 'RESULT smi boundary '; show: (SmallInteger maxVal + 1) printString; show: ' '; show: (SmallInteger minVal - 1) printString; show: ' '; show: (SmallInteger maxVal * SmallInteger maxVal) printString; cr!
Transcript show: 'RESULT large division '; show: (100 factorial // 49 factorial) printString; show: ' '; show: (100 factorial \\ 1000000007) printString; cr!
Transcript show: 'RESULT smi arithmetic '; show: ((1 to: 30000) inject: 0 into: [ :a :b | (a + (b * b \\ 7919)) \\ 1000003 ]) printString; cr!
Transcript show: 'RESULT smi overflow '; show: ((1 to: 70) inject: 1 into: [ :a :b | a * 3 + b ]) printString; cr!
Transcript show: 'RESULT negative '; show: ((1 to: 5000) inject: 0 into: [ :a :b | a - (b // 3) + (b negated \\ 5) - (b negated // 7) + (b negated rem: 3) + (b negated quo: 4) ]) printString; cr!
Transcript show: 'RESULT bit ops '; show: ((1 to: 5000) inject: 0 into: [ :a :b | (a bitXor: (b bitShift: 3)) bitAnd: 16rFFFFFF ]) printString; cr!
Transcript show: 'RESULT floats '; show: ((1 to: 5000) inject: 0.0 into: [ :a :b | a + (b asFloat sqrt / (b + 1)) ]) printString; cr!
Transcript show: 'RESULT float compare '; show: ((1 to: 5000) inject: 0 into: [ :a :b | (b asFloat / 7.0) > (b // 7) asFloat ifTrue: [ a + 1 ] ifFalse: [ a ] ]) printString; cr!
Transcript show: 'RESULT float conversion '; show: ((1 to: 2000) inject: 0 into: [ :a :b | a + (b asFloat * 1.37) truncated + (b asFloat / 3.0) rounded ]) printString; cr!
Transcript show: 'RESULT sieve '; show: [ | flags count |
	count := 0.
	flags := Array new: 8190.
	1 to: 8190 do: [ :i | flags at: i put: true ].
	2 to: 8190 do: [ :i | (flags at: i) ifTrue: [ count := count + 1. i + i to: 8190 by: i do: [ :k | flags at: k put: false ] ] ].
	count ] value printString; cr!
Transcript show: 'RESULT strings '; show: ((1 to: 300) inject: '' into: [ :s :i | (s , i printString) size > 60 ifTrue: [ i printString ] ifFalse: [ s , i printString ] ]); cr!
Transcript show: 'RESULT string ops '; show: ('Hello, Strongtalk on Apple Silicon' collect: [ :c | c isVowel ifTrue: [ c asUppercase ] ifFalse: [ c asLowercase ] ]); show: ' '; show: 'abcdef' reverse; show: ' '; show: ('hello world' indexOf: $w) printString; cr!
Transcript show: 'RESULT symbols '; show: ((Array with: #foo with: #bar with: #baz with: #quux) inject: 0 into: [ :a :s | a + s size ]) printString; cr!
Transcript show: 'RESULT ordered collection '; show: [ | oc | oc := OrderedCollection new. 1 to: 3000 do: [ :i | oc addFirst: i. i \\ 3 = 0 ifTrue: [ oc removeLast ] ]. oc size printString , ' ' , oc first printString , ' ' , oc last printString ] value; cr!
Transcript show: 'RESULT dictionary '; show: [ | d | d := Dictionary new. 1 to: 2000 do: [ :i | d at: i \\ 173 put: i ]. (d keys inject: 0 into: [ :a :k | a + (d at: k) ]) printString ] value; cr!
Transcript show: 'RESULT set '; show: [ | s | s := Set new. 1 to: 5000 do: [ :i | s add: i * i \\ 1013 ]. s size printString ] value; cr!
Transcript show: 'RESULT sort '; show: [ | sc | sc := SortedCollection sortBlock: [ :a :b | a > b ]. 1 to: 2000 do: [ :i | sc add: (i * 7919) \\ 1009 ]. (sc asArray copyFrom: 1 to: 5) printString ] value; cr!
Transcript show: 'RESULT blocks '; show: [ | b r | r := 0. b := [ :x | x > 0 ifTrue: [ r := r + x ] ]. 1 to: 10000 do: [ :i | b value: i - 5000 ]. r printString ] value; cr!
Transcript show: 'RESULT non-local return '; show: ((1 to: 3000) inject: 0 into: [ :a :i | a + ([ :n | (1 to: n) do: [ :k | k * k > n ifTrue: [ k ] ]. 0 ] value: i) ]) printString; cr!
Transcript show: 'RESULT detect '; show: ((1 to: 3000) inject: 0 into: [ :a :i | a + ((1 to: i) detect: [ :k | k * k >= i ] ifNone: [ 0 ]) ]) printString; cr!
Transcript show: 'RESULT recursion '; show: [ | fib | fib := nil. fib := [ :n | n < 2 ifTrue: [ n ] ifFalse: [ (fib value: n - 1) + (fib value: n - 2) ] ]. (fib value: 22) printString ] value; cr!
Transcript show: 'RESULT tak '; show: (TakBenchmark new takX: 18 Y: 12 Z: 6) printString; cr!
Transcript show: 'RESULT ensure '; show: [ | n | n := 0. 1 to: 500 do: [ :i | [ n := n + i ] ensure: [ n := n + 1 ] ]. n printString ] value; cr!
LargeInteger test. Transcript show: 'RESULT LargeInteger test done'; cr!
