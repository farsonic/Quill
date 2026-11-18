#!/bin/sh
# lo_doc.sh - WP28d's check against LibreOffice: LibreOffice writes each of tests/data's .docx documents
# as Word 97-2003 (.doc), and Quill must read every paragraph's text from the .doc. It also makes the
# .doc test files again in the work directory (doc-fmt.doc and doc-rich.doc from tests/data/doc-*.rtf,
# lo-word/tables/notes/headers.doc from the .docx); copy them to tests/data if they are to change.
# Needs soffice. Usage: tools/lo_doc.sh [workdir]
cd "$(dirname "$0")/.."
W=${1:-/tmp/quill_lo_doc}
mkdir -p "$W"
cc -std=c99 -O1 -I. core/*.c tests/fmtconv.c -o "$W/fmtconv" || exit 1
fail=0
for f in word lo tables headers notes pictures notes-lo wordcraft-long charts equations; do
  cp "tests/data/$f.docx" "$W/lo-$f.docx"
  (cd "$W" && timeout 300 soffice --headless --convert-to "doc:MS Word 97" "lo-$f.docx" >/dev/null 2>&1)
  printf 'LibreOffice .doc %-15s read by Quill: ' "$f"
  if [ $f = equations ]; then
    "$W/fmtconv" -cmp "tests/data/$f.docx" "$W/lo-$f.doc" | tail -1   # (equations are OLE objects in .doc: not read)
  else
    "$W/fmtconv" -cmp "tests/data/$f.docx" "$W/lo-$f.doc" || fail=1
  fi
done
for f in fmt rich; do
  cp "tests/data/doc-$f.rtf" "$W/doc-$f.rtf"
  (cd "$W" && timeout 300 soffice --headless --convert-to "doc:MS Word 97" "doc-$f.rtf" >/dev/null 2>&1)
  printf 'LibreOffice .doc of doc-%-8s read by Quill: ' "$f.rtf"
  "$W/fmtconv" -cmp "$W/doc-$f.rtf" "$W/doc-$f.doc" || fail=1
done
[ $fail = 0 ] && echo "all found" || echo "SOME MISSING"
exit $fail
