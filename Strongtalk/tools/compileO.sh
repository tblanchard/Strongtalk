#!/bin/sh
# Copyright 1996, Animorphic Systems. $Revision: 1.3 $
# Converted from compileO.bat.
# Original: set PATH/INCLUDE/LIB from %MSDEVDIR% and run Microsoft NMAKE
# (Windows-only). This shell variant exports the same variables and uses make.
PATH="${MSDEVDIR}/bin:${PATH}"
export PATH
INCLUDE="${MSDEVDIR}/INCLUDE"
export INCLUDE
LIB="${MSDEVDIR}/LIB"
export LIB
make -f Delta.mak "CFG=Delta - Win32 Fast"
