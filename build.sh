#!/bin/sh
# Build Quill for AmigaOS (vbcc + NDK 3.2 + MUI 3.8 headers) and run the native core tests.
set -e
export VBCC=${VBCC:-/opt/amiga/vbcc}
export PATH=$VBCC/bin:$PATH
NDK=${NDK:-/opt/amiga/ndk/Include_H}
MUI=${MUI:-/opt/amiga/mui/include}
cd "$(dirname "$0")"
CORE="core/util.c core/doc.c core/table.c core/fonts.c core/layout.c core/zip.c core/xml.c core/docx.c core/pdf.c core/image.c core/template.c core/toc.c core/spell.c core/hyph.c core/tstyle.c core/ttraster.c"
cc -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined $CORE tests/coretest.c -o /tmp/quill_coretest
/tmp/quill_coretest | tail -3
if [ -f amiga/main.c ]; then
  vc +aos68k -c99 -O2 -cpu=68040 -I. -I"$MUI" -I"$NDK" amiga/*.c amiga/hook.s $CORE -o Quill -lamiga
fi
