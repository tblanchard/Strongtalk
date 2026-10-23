#!/bin/zsh
# Builds the release VM (/tmp/stobj_rel) if needed and starts the Strongtalk GUI in the image
# directory (strongtalk-source-r36: strongtalk.bst, source/, resources/, startPage.html).
# Extra arguments go to the VM. Environment: STRONGTALK_TRACE_GUI=1 lists unimplemented
# Win32 functions; STRONGTALK_GUI_SCRIPT runs a GUI test script (see win32User.mm).
here=${0:A:h}
RELEASE=1 $here/stlink.sh | tail -1
cd $here/../..
exec /tmp/stobj_rel/strongtalk "$@"
