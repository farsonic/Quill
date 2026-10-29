#!/bin/sh
# lo_formats.sh - WP28a/WP28c's check against LibreOffice (and pandoc for Markdown): Quill writes each
# of tests/data's .docx documents as RTF, HTML and Markdown; LibreOffice converts the RTF and HTML to
# .docx (pandoc the Markdown); Quill reads that back and every paragraph's text must be there. Then
# LibreOffice writes RTF and HTML of the same documents and Quill must read those. Needs soffice
# (and pandoc). Usage: tools/lo_formats.sh [workdir]
cd "$(dirname "$0")/.."
W=${1:-/tmp/quill_lo_formats}
mkdir -p "$W"
cc -std=c99 -O1 -I. core/*.c tests/fmtconv.c -o "$W/fmtconv" || exit 1
fail=0
for f in word lo tables headers notes pictures notes-lo; do
  src=tests/data/$f.docx
  for ext in rtf html md; do
    "$W/fmtconv" "$src" "$W/q-$f.$ext" || { echo "FAIL Quill writing $f.$ext"; fail=1; continue; }
    rm -f "$W/q-$f.docx" 2>/dev/null
    if [ $ext = md ]; then
      command -v pandoc >/dev/null || continue
      pandoc -f gfm "$W/q-$f.md" -o "$W/q-$f.docx" 2>/dev/null
      who=pandoc
    else
      (cd "$W" && timeout 180 soffice --headless --convert-to docx:"MS Word 2007 XML" "q-$f.$ext" >/dev/null 2>&1)
      who=LibreOffice
    fi
    printf '%s %-9s via %-11s ' "$ext" "$f" "$who"
    "$W/fmtconv" -cmp "$src" "$W/q-$f.docx" || fail=1
  done
  for ext in rtf html; do
    (cd "$W" && cp "$OLDPWD/$src" "lo-$f.docx" && timeout 180 soffice --headless --convert-to $ext "lo-$f.docx" >/dev/null 2>&1)
    printf 'LibreOffice %-4s %-9s read by Quill: ' "$ext" "$f"
    "$W/fmtconv" -cmp "$src" "$W/lo-$f.$ext" || fail=1
  done
done
[ $fail = 0 ] && echo "all found" || echo "SOME MISSING"
exit $fail
