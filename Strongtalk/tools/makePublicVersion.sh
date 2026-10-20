#!/bin/sh
# Copyright 1994, Animorphic Systems.
# Converted from makePublicVersion.bat; executable from /bin/sh.
# Original copied a subset of the vm tree to a destination drive (v:) and
# built it with NMAKE/make. Paths use forward slashes for /bin/sh.
src="${DeltaDir}"
dst="/v/$1"
mkdir -p "$dst/vm"
for d in asm code compiler disasm incls interpreter lookup memory oops prims \
         recompiler runtime topIncludes utilities bin; do
  mkdir -p "$dst/vm/$d"
  cp -r "$src/vm/$d/." "$dst/vm/$d/"
done
sed -e "s/USERS/users/g" < "$src/vm/bin/Delta.mak" \
    | sed -e "s/users.larsp.Delta/$1/g" > "$dst/vm/bin/Delta.mak"
cp "$src/vm/bin/Delta.mdp" "$dst/vm/bin"
mkdir -p "$dst/vm/bin/asm_obj"
cp "$src/vm/bin/asm_obj/Makefile" "$dst/vm/bin/asm_obj"
cd "$dst/vm/bin/asm_obj" || exit
make
cd "$dst/vm/bin" || exit
compileO
