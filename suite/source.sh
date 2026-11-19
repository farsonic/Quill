#!/bin/sh
# source.sh version workdir: builds Bureau-<version>-src.lha, the source code of the suite package
# (suite/package.sh): one drawer Bureau-<version>-src/ with README-Bureau.md, LICENSE and, per
# program, the source its single-app package carries in Source/: the files the checkout tracks in
# git, as they are in the working tree, without the notes for whoever works on it (CLAUDE.md), the
# GitHub settings, and for Easel without the tests' generated pictures (its package.sh keeps
# tests/*.c and tests/*.py). Checkouts as for package.sh (QUILL, LEDGER, EASEL, FOLIO, DISPATCH).
# Then the archive is searched for certificate or key material: key or certificate files refuse it
# (moved aside as .REFUSED), and so does a PEM block with a body, wherever it is (source text that
# only names the armour, a parser's or a test's, is allowed).
set -e
V=$1; W=$2
[ -n "$V" ] && [ -n "$W" ] || { echo "usage: source.sh version workdir"; exit 2; }
S=$(cd "$(dirname "$0")" && pwd)
QUILL=${QUILL:-/home/user/quill}
LEDGER=${LEDGER:-/home/user/ledger}
EASEL=${EASEL:-/home/user/easel}
FOLIO=${FOLIO:-/home/user/folio}
DISPATCH=${DISPATCH:-/home/user/farsonic/dispatch}
mkdir -p "$W"
W=$(cd "$W" && pwd)
NAME=Bureau-$V-src
B=$W/$NAME
[ -e "$B" ] && { echo "$B is there already: give a new workdir"; exit 2; }
mkdir -p "$B"
cp "$S/../README-Bureau.md" "$QUILL/LICENSE" "$B/"
for A in Quill Ledger Easel Folio Dispatch; do
  case $A in Quill) R=$QUILL;; Ledger) R=$LEDGER;; Easel) R=$EASEL;; Folio) R=$FOLIO;; Dispatch) R=$DISPATCH;; esac
  [ -d "$R/.git" ] || [ -f "$R/.git" ] || { echo "$R is not a git checkout"; exit 2; }
  mkdir -p "$B/$A"
  (cd "$R" && git ls-files -z | tr '\0' '\n' |
     grep -v -e '^CLAUDE\.md$' -e '^\.github/' -e '^\.gitignore$' |
     if [ "$A" = Easel ]; then grep -E -v '^docs/|^tests/' || true; else cat; fi |
     { cat; [ "$A" = Easel ] && git ls-files | grep -E '^tests/[^/]+\.(c|py)$' || true; } |
     while IFS= read -r f; do [ -f "$f" ] && echo "$f"; done |
     tar -cf - -T - | (cd "$B/$A" && tar -xf -))
  # which commit of which branch each came from
  echo "$A: $(find "$B/$A" -type f | wc -l) files from $R ($(git -C "$R" rev-parse --short HEAD), $(git -C "$R" rev-parse --abbrev-ref HEAD))"
done
N=$W/$NAME.lha
T=$W/.$NAME.lha.new
(cd "$W" && lha -aq "$T" "$NAME" >/dev/null 2>&1)
mv "$T" "$N"

# the release must never carry a certificate or key
D=$(mktemp -d "$W/schk.XXXX")
(cd "$D" && lha -xq "$N" >/dev/null 2>&1)
[ -f "$D/$NAME/LICENSE" ] || { echo "the archive did not unpack"; exit 1; }
if find "$D" -iname '*.crt' -o -iname '*.pem' -o -iname '*.key' -o -iname '*.p12' -o -iname '*.pfx' | grep . ||
   python3 - "$D" <<'PY'
import os, re, sys
# a PEM armour line followed by a base64 body is material; the armour named in code is not
arm = re.compile(rb'-----BEGIN (CERT' rb'IFICATE|(RSA |EC |ENCRYPTED )?PRIVATE KEY)-----\r?\n([A-Za-z0-9+/=]{40,})')
hit = 0
for root, dirs, files in os.walk(sys.argv[1]):
    for f in files:
        p = os.path.join(root, f)
        with open(p, 'rb') as fh:
            if arm.search(fh.read()):
                print(p); hit = 1
sys.exit(0 if hit else 1)
PY
then
  mv "$N" "$N.REFUSED"
  echo "CERTIFICATE OR KEY MATERIAL FOUND: $N moved aside as $N.REFUSED"
  exit 1
fi
echo "certificate and key check: clean"
ls -l "$N"
