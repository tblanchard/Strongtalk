import re, sys, collections
# Hoist MSVC-style for-loop variables used after their loop.
# Input: clang errors "file:line:col: error: use of undeclared identifier 'v'"
import os
base=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Strongtalk', 'vm', '')
errs=collections.defaultdict(set)
for line in open('/tmp/stlogs/uniq_errors.txt'):
    m=re.match(r"([^:]+):(\d+):\d+: error: use of undeclared identifier '(\w+)'(; did you mean '(\w+)'\?)?$", line.strip())
    m2=re.match(r"([^:]+):(\d+):\d+: error: (non-object type 'char \*\(const char \*, int\)' is not assignable|array subscript is not an integer|cannot increment value of type 'char \*\(const char \*, int\)')", line.strip())
    if m2:
        errs[m2.group(1)].add((int(m2.group(2)),'index')); continue
    if not m: continue
    f,l,v=m.group(1),int(m.group(2)),m.group(3)
    if m.group(4) and m.group(4)!=v: continue
    if len(v)>12: continue
    errs[f].add((l,v))
fixed=0
for f,items in errs.items():
    lines=open(base+f,encoding='latin-1').read().split('\n')
    done=set()
    for l,v in sorted(items):
        pat=re.compile(r'for\s*\(\s*(int|unsigned int|unsigned|smi|[A-Za-z_]\w*\s*\*|[A-Za-z_]\w*)\s+'+v+r'\b')
        for k in range(l-1, max(-1,l-400), -1):
            mm=pat.search(lines[k])
            if mm:
                if (k,v) in done: break
                ty=mm.group(1)
                indent=re.match(r'\s*',lines[k]).group(0)
                lines[k]=lines[k][:mm.start()]+'for ('+lines[k][mm.end()-len(v):]
                lines.insert(k, f"{indent}{ty} {v};")
                done.add((k,v)); fixed+=1
                # shift later error lines
                items={(ll+1 if ll>k else ll, vv) for ll,vv in items}
                break
    open(base+f,'w',encoding='latin-1').write('\n'.join(lines))
print("hoisted",fixed)
