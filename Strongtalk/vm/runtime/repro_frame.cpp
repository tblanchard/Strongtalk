// Reproduction: types.hpp forward-declares 'class frame;' (incomplete),
// but ValueObj is NOT declared, so frame.hpp's 'class frame : ValueObj'
// definition is aborted, leaving 'frame' incomplete at vframe.hpp:50.
#include "../topIncludes/types.hpp"
#include "vframe.hpp"

// Scratch reproduction, not part of the VM; only built on request.
#ifdef STRONGTALK_REPRO_FRAME
int main() { return 0; }
#endif
