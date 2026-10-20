import re,os
os.chdir('/Users/todd/Projects/strongtalk-source-r36/Strongtalk/Strongtalk/vm')
def rd(p): return open(p,encoding='latin-1').read()
def wr(p,s): open(p,'w',encoding='latin-1').write(s)
s=rd('code/nmethod.hpp'); wr('code/nmethod.hpp', s.replace('#include "nmethodScopes.hpp"\n','',1))
s=rd('compiler/compUtils.cpp'); wr('compiler/compUtils.cpp', s.replace('void PerformanceDebugger::report_block(Node* n, BlockPReg* blk, char* what) {','void PerformanceDebugger::report_block(Node* n, BlockPReg* blk, const char* what) {'))
s=rd('compiler/defUse.cpp'); wr('compiler/defUse.cpp', re.sub(r'(\s)char\*(\s+prop = ok \?)', r'\1const char*\2', s))
s=rd('interpreter/prettyPrinter.cpp'); lines=s.split('\n')
for i,t in enumerate(lines):
    if t.strip()=='char* string()  { return str;  }' and i in (256,289): lines[i]=t.replace('char* string()','const char* string()')
wr('interpreter/prettyPrinter.cpp','\n'.join(lines))
for p,h in [('runtime/process.hpp','growableArray.hpp'),('runtime/timer.hpp','longInt.hpp')]:
    s=rd(p)
    if f'"{h}"' not in s:
        m=re.search(r'^#\s*define \w+\s*\n', s, re.M); wr(p, s[:m.end()]+f'#include "{h}"\n'+s[m.end():])
print("batch8 done")
