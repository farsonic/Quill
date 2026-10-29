#!/bin/sh
# lo_odt.sh - WP28b's check against LibreOffice: Quill writes each of tests/data's .docx documents as
# .odt, LibreOffice converts that to .docx and to PDF; Quill reads the .docx back and every paragraph's
# text must be there (the PDFs are for looking at, next to LibreOffice's own PDF of the .docx). Then
# LibreOffice writes .odt of the same documents and Quill must read those. Needs soffice.
# Usage: tools/lo_odt.sh [workdir]
cd "$(dirname "$0")/.."
W=${1:-/tmp/quill_lo_odt}
mkdir -p "$W"
cc -std=c99 -O1 -I. core/*.c tests/fmtconv.c -o "$W/fmtconv" || exit 1
fail=0
for f in word lo tables headers notes pictures notes-lo equations charts wordcraft-long; do
  src=tests/data/$f.docx
  "$W/fmtconv" "$src" "$W/q-$f.odt" || { echo "FAIL Quill writing $f.odt"; fail=1; continue; }
  (cd "$W" && timeout 180 soffice --headless --convert-to docx:"MS Word 2007 XML" "q-$f.odt" >/dev/null 2>&1 &&
   timeout 180 soffice --headless --convert-to pdf "q-$f.odt" >/dev/null 2>&1)
  printf 'odt %-15s via LibreOffice: ' "$f"
  "$W/fmtconv" -cmp "$src" "$W/q-$f.docx" || fail=1
  (cd "$W" && cp "$OLDPWD/$src" "lo-$f.docx" && timeout 180 soffice --headless --convert-to odt "lo-$f.docx" >/dev/null 2>&1)
  printf 'LibreOffice odt %-15s read by Quill: ' "$f"
  "$W/fmtconv" -cmp "$src" "$W/lo-$f.odt" || fail=1
done
[ $fail = 0 ] && echo "all found" || echo "SOME MISSING"
exit $fail
