#!/bin/sh
# Build Quill for AmigaOS (vbcc + NDK 3.2 + MUI 3.8 headers) and run the native core tests.
set -e
export VBCC=${VBCC:-/opt/amiga/vbcc}
export PATH=$VBCC/bin:$PATH
NDK=${NDK:-/opt/amiga/ndk/Include_H}
MUI=${MUI:-/opt/amiga/mui/include}
cd "$(dirname "$0")"
# every core/*.c and every package's tests/test_wp*.c (a work package's new files need no line here)
CORE=$(echo core/*.c)
cc -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined $CORE tests/coretest.c tests/test_wp*.c -o /tmp/quill_coretest_$(basename "$(pwd)")
/tmp/quill_coretest_$(basename "$(pwd)") > /tmp/quill_coretest_$(basename "$(pwd)").txt || { cat /tmp/quill_coretest_$(basename "$(pwd)").txt; echo "native tests failed"; exit 1; }
tail -3 /tmp/quill_coretest_$(basename "$(pwd)").txt
if [ -f amiga/main.c ]; then
  vc +aos68k -c99 -O2 -cpu=68040 -I. -I"$MUI" -I"$NDK" amiga/*.c amiga/hook.s $CORE -o Quill -lamiga
fi
