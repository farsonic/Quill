#!/bin/sh
# package.sh version workdir: builds Bureau-<version>.lha, the suite package: Quill, Ledger, Easel,
# Folio and Dispatch with one Fonts, one TrueType and one Dict drawer between them.
#
#   sh suite/package.sh 0.1 /tmp/bureau-pkg
#
# It takes each program from its checkout, already built (it refuses if a binary is missing):
#   QUILL=/home/user/quill  LEDGER=/home/user/ledger  EASEL=/home/user/easel
#   FOLIO=/home/user/folio  DISPATCH=/home/user/farsonic/dispatch   (the defaults)
# The workdir must be new (or empty): nothing is deleted, a workdir already used is refused.
#
# The archive holds one drawer, Bureau/ (with Bureau.info beside it):
#   Install-Bureau, README-Bureau.md, LICENSE (GPL-2.0, the same text in all five repositories)
#   Fonts/     Quill's screen fonts (fonts-amiga/); the installer puts them in FONTS:
#   TrueType/  Quill's fonts/ (Liberation, Carlito, Caladea; SIL OFL in LICENSE-fonts.txt)
#   Dict/      Quill's dict/ (word list, thesaurus, hyphenation; their licences), a superset of
#              what Ledger and Easel ship
#   Quill/ Ledger/ Easel/ Folio/ Dispatch/   each program with its README.md, its example files and
#              its own licence notes (Easel's and Dispatch's Licenses.txt), without Fonts, TrueType,
#              Dict or Source (suite/source.sh makes Bureau-<version>-src.lha)
# The example files come from the checkout where it has them (Ledger's FunctionTests.xlsx, Easel's
# Example.jpg, Folio's sample/Welcome.pdf), else from that program's newest single-app package
# beside its checkout (<App>-*.lha: Quill's *.docx, Ledger's Example.xlsx).
# Icons: each program's drawer gets the icons its own tools/mkicons.py draws; suite/mkicons.py draws
# Bureau's own (the drawer, the installer, the README, the shared drawers) and tidies the rows.
# Last, the archive is unpacked again and searched for certificate or key material: if any is
# found, the archive is moved aside as .REFUSED and the script fails.
set -e
V=$1; W=$2
[ -n "$V" ] && [ -n "$W" ] || { echo "usage: package.sh version workdir"; exit 2; }
S=$(cd "$(dirname "$0")" && pwd)
QUILL=${QUILL:-/home/user/quill}
LEDGER=${LEDGER:-/home/user/ledger}
EASEL=${EASEL:-/home/user/easel}
FOLIO=${FOLIO:-/home/user/folio}
DISPATCH=${DISPATCH:-/home/user/farsonic/dispatch}
mkdir -p "$W"
W=$(cd "$W" && pwd)
B=$W/Bureau
[ -e "$B" ] && { echo "$B is there already: give a new workdir"; exit 2; }

# ------------------------------------------------------------------ the programs must be built

for f in "$QUILL/Quill" "$LEDGER/Ledger" "$EASEL/Easel" "$EASEL/EaselConvert" "$FOLIO/Folio" \
         "$DISPATCH/Dispatch" "$DISPATCH/DispatchConvert"; do
  [ -f "$f" ] || { echo "missing $f: build it first (./build.sh in its checkout)"; exit 2; }
  # an AmigaOS executable starts with the hunk header 000003F3
  [ "$(od -An -tx1 -N4 "$f" | tr -d ' \n')" = "000003f3" ] || { echo "$f is not an AmigaOS executable"; exit 2; }
done
for f in "$QUILL/fonts-amiga/QuillSans.font" "$QUILL/fonts/LICENSE-fonts.txt" "$QUILL/dict/british-english" \
         "$QUILL/LICENSE" "$S/Install-Bureau" "$S/../README-Bureau.md" "$S/mkicons.py" "$EASEL/Licenses.txt" "$DISPATCH/Licenses.txt"; do
  [ -f "$f" ] || { echo "missing $f"; exit 2; }
done

# newest single-app package of a program (not a source archive), or nothing
newest() { ls -t "$1"/"$2"-[0-9]*.lha 2>/dev/null | grep -v -e '-src\.lha$' | head -1; }
# extract the top-level files of a program's single package whose names match an extended regex
from_single() {  # repo app regex dest
  P=$(newest "$1" "$2")
  [ -n "$P" ] || { echo "no $2-*.lha beside $1 for its example files"; exit 2; }
  X=$W/singles/$2
  mkdir -p "$X"
  NAMES=$(lha -lq "$P" 2>/dev/null | awk '{print $NF}' | grep -E "^$2/$3\$" || true)
  [ -n "$NAMES" ] || { echo "no files matching $3 in $P"; exit 2; }
  # shellcheck disable=SC2086
  (cd "$X" && lha -xq "$P" $NAMES >/dev/null 2>&1)
  for n in $NAMES; do cp "$X/$n" "$4/"; done
  echo "  from $(basename "$P"): $(echo $NAMES | sed "s|$2/||g")"
}

mkdir -p "$B"
echo "Bureau $V in $B"

# ------------------------------------------------------------------ shared drawers

cp -r "$QUILL/fonts-amiga" "$B/Fonts"
mkdir -p "$B/TrueType" "$B/Dict"
cp "$QUILL"/fonts/*.ttf "$QUILL/fonts/LICENSE-fonts.txt" "$QUILL/fonts/fonts.list" "$B/TrueType/"
sed -i "s/are not part of Quill's own code/are not part of the Bureau programs' own code/" "$B/TrueType/LICENSE-fonts.txt"
sed -i "s/fonts Quill measures text with/fonts Quill, Ledger, Easel, Folio and Dispatch measure text with/" "$B/TrueType/fonts.list"
for f in "$QUILL"/dict/*; do
  case "$(basename "$f")" in README) ;; *) cp "$f" "$B/Dict/" ;; esac
done
cat > "$B/Dict/README" <<'TXT'
The word list, thesaurus and hyphenation patterns of Bureau's programs, shared by all of them
(Bureau:Dict). They look for this drawer beside themselves first (PROGDIR:Dict), then here.

british-english: the word list Quill, Ledger and Easel check spelling with, one word a line
(UTF-8). It is SCOWL's British English list (size 50, from Debian's wbritish 2020.12.07) without
the possessive forms ("'s"; they are accepted for any word), with a few dozen words added that
documents about computers often use (footer, endnote, email, website...). SCOWL is Copyright
2000-2011 Kevin Atkinson and others; see LICENSE-dict.txt for its terms.

Words added with "Add to Dictionary" go to <program>.words beside each program (Quill.words,
Ledger.words, Easel.words), not here, so updating Bureau keeps them.

th_en.dat: the English thesaurus (Quill's Review > Thesaurus), in MyThes's format, made by Quill's
tools/mkthes.py from LibreOffice's th_en_US_v2 (made from Princeton's WordNet): the words in
british-english, up to four meanings of six synonyms each. WordNet's licence is in
LICENSE-thesaurus.txt.

hyph_*.dic: hyphenation patterns (Quill's text, Easel's paragraph type), made by Quill's
tools/mkhyph.py from the hyph-utf8 patterns; see each .txt, README_hyph_en_GB.txt and
LICENSE-hyph.txt.

Other languages (Quill): put a Hunspell dictionary here, named by its language (de_DE.aff and
de_DE.dic, or de.aff and de.dic); th_de_DE.dat (a MyThes thesaurus) and hyph_de_DE.dic (Hyphen
patterns) work the same way.
TXT

# ------------------------------------------------------------------ the programs

echo "Quill"
mkdir -p "$B/Quill"
cp "$QUILL/Quill" "$QUILL/README.md" "$B/Quill/"
from_single "$QUILL" Quill '[^/]+\.docx' "$B/Quill"

echo "Ledger"
mkdir -p "$B/Ledger"
cp "$LEDGER/Ledger" "$LEDGER/README.md" "$LEDGER/FunctionTests.xlsx" "$B/Ledger/"
from_single "$LEDGER" Ledger 'Example[^/]*\.(xlsx|csv)' "$B/Ledger"

echo "Easel"
mkdir -p "$B/Easel"
cp "$EASEL/Easel" "$EASEL/EaselConvert" "$EASEL/README.md" "$EASEL/Licenses.txt" "$EASEL/Example.jpg" "$B/Easel/"

echo "Folio"
mkdir -p "$B/Folio"
cp "$FOLIO/Folio" "$FOLIO/README.md" "$FOLIO/sample/Welcome.pdf" "$B/Folio/"

echo "Dispatch"
mkdir -p "$B/Dispatch"
cp "$DISPATCH/Dispatch" "$DISPATCH/DispatchConvert" "$DISPATCH/README.md" "$DISPATCH/Licenses.txt" "$B/Dispatch/"

# ------------------------------------------------------------------ Bureau's own files

cp "$S/Install-Bureau" "$S/../README-Bureau.md" "$QUILL/LICENSE" "$B/"
sed -i "s/^(set #version \"[^\"]*\")/(set #version \"$V\")/" "$B/Install-Bureau"

# ------------------------------------------------------------------ icons

# each program's own tools/mkicons.py, run on a staging copy of its drawer (hard links) that sees
# the shared TrueType drawer (for the icons' lettering); only the icons of what is in Bureau are kept
for A in Quill Ledger Easel Folio Dispatch; do
  case $A in Quill) R=$QUILL;; Ledger) R=$LEDGER;; Easel) R=$EASEL;; Folio) R=$FOLIO;; Dispatch) R=$DISPATCH;; esac
  G=$W/stage/$A
  mkdir -p "$G"
  cp -al "$B/$A" "$G/$A"
  ln -s "$B/TrueType" "$G/$A/TrueType"
  python3 "$R/tools/mkicons.py" "$G" >/dev/null
  cp "$G/$A.info" "$B/"
  for i in "$G/$A"/*.info; do
    n=$(basename "$i" .info)
    [ -f "$B/$A/$n" ] && cp "$i" "$B/$A/"
  done
done
python3 "$S/mkicons.py" "$W"

# ------------------------------------------------------------------ the archive

N=$W/Bureau-$V.lha
T=$W/.Bureau-$V.lha.new
(cd "$W" && lha -aq "$T" Bureau Bureau.info >/dev/null 2>&1)
mv "$T" "$N"

# the release must never carry a certificate or key
D=$(mktemp -d "$W/bchk.XXXX")
(cd "$D" && lha -xq "$N" >/dev/null 2>&1)
[ -f "$D/Bureau/Quill/Quill" ] || { echo "the archive did not unpack"; exit 1; }
if grep -rlE "BEGIN CERT""IFICATE|BEGIN (RSA |EC |ENCRYPTED )?PRIVATE KEY|sandbox-test""-only" "$D" ||
   find "$D" -iname '*.crt' -o -iname '*.pem' -o -iname '*.key' -o -iname '*.p12' -o -iname '*.pfx' | grep .; then
  mv "$N" "$N.REFUSED"
  echo "CERTIFICATE OR KEY MATERIAL FOUND: $N moved aside as $N.REFUSED"
  exit 1
fi
echo "certificate and key check: clean"
ls -l "$N"
