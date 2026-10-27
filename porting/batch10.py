# SmallIntegers are 62 bits on 64-bit hosts: check indices/sizes at full width before narrowing to int
import re, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Strongtalk', 'vm'))
def rd(p): return open(p, encoding='latin-1').read()
def wr(p, s): open(p, 'w', encoding='latin-1').write(s)

for p in ['oops/byteArrayOop.hpp', 'oops/dByteArrayOop.hpp', 'oops/objArrayOop.hpp', 'oops/dValueArrayOop.hpp']:
    s = rd(p)
    s = s.replace('bool is_within_bounds(int index) const { return 1 <= index && index <= length(); }',
                  'bool is_within_bounds(smi index) const { return 1 <= index && index <= length(); }')
    wr(p, s)

# Maximum array allocation in bytes: object sizes are ints (in words) inside the VM.
s = rd('memory/util.hpp')
if 'max_array_bytes' not in s:
    s = s.replace('const int oopSize   = sizeof(oop);', '''const int oopSize   = sizeof(oop);

// Largest array body (in bytes) the allocation primitives accept; object sizes are ints inside the VM.
const smi max_array_bytes = INT_MAX / 2;''')
    wr('memory/util.hpp', s)

for p, elem in [('prims/objArray_prims.cpp', 'oopSize'), ('prims/byteArray_prims.cpp', '1'),
                ('prims/dByteArray_prims.cpp', 'sizeof(doubleByte)'), ('prims/dValueArray_prims.cpp', 'sizeof(double)')]:
    s = rd(p)
    a = '''  if (smiOop(argument)->value() < 0)
    return markSymbol(vmSymbols::negative_size());
'''
    b = a + f'''  if (smiOop(argument)->value() > max_array_bytes / {elem})
    return markSymbol(vmSymbols::argument_is_invalid());
'''
    if 'max_array_bytes' not in s:
        n = s.count(a)
        s = s.replace(a, b)
        print(p, 'allocation checks added:', n)
    wr(p, s)
print('batch10 done')
