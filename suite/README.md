# suite/ - the Bureau package

Bureau is the suite package of Quill, Ledger, Easel, Folio and Dispatch: one archive, one
installer, the shared screen fonts, TrueType fonts and dictionaries in it once. What it is for users
is in `README-Bureau.md` at the top of this repository (it goes into the package). This drawer
makes it.

| File | What |
|---|---|
| `package.sh <version> <workdir>` | builds `<workdir>/Bureau-<version>.lha` from the five checkouts |
| `source.sh <version> <workdir>` | builds `<workdir>/Bureau-<version>-src.lha`, the five programs' source |
| `Install-Bureau` | the Installer script (Commodore's Installer 43 or AROS's) |
| `mkicons.py <workdir>` | Bureau's own icons (drawer, installer, README, LICENSE, shared drawers), with Quill's `tools/mkicons.py` helpers; run by `package.sh` |

## Building the packages

Build each program first (`./build.sh` in each checkout; `package.sh` refuses a missing binary or
one that isn't an AmigaOS executable), then:

    sh suite/package.sh 0.1 /tmp/bureau-0.1
    sh suite/source.sh 0.1 /tmp/bureau-0.1

The checkouts are found at `QUILL=/home/user/quill`, `LEDGER=/home/user/ledger`,
`EASEL=/home/user/easel`, `FOLIO=/home/user/folio` and `DISPATCH=/home/user/farsonic/dispatch`
(set those to use others). The workdir must be new: nothing is deleted, a used one is refused.

What goes in (`package.sh`):

- `Bureau/Fonts`: Quill's `fonts-amiga/`; `Bureau/TrueType`: Quill's `fonts/` (the `.ttf` files,
  `LICENSE-fonts.txt`, `fonts.list`); `Bureau/Dict`: Quill's `dict/` (a superset of the Dict
  drawers Ledger and Easel ship) with a README for the suite.
- `Bureau/<App>`: the program (Easel and Dispatch with their Shell converters), its README.md, its
  example files and its `Licenses.txt` (Easel, Dispatch). Examples come from the checkout where it
  has them (Ledger's `FunctionTests.xlsx`, Easel's `Example.jpg`, Folio's `sample/Welcome.pdf`),
  else from the program's newest single-app package beside its checkout (`<App>-*.lha`: Quill's
  `*.docx`, Ledger's `Example.xlsx`).
- `Bureau/Install-Bureau` (its `#version` set to the version given), `README-Bureau.md`, `LICENSE`.
- Icons: each program's drawer gets what that repository's `tools/mkicons.py` draws (run on a
  staging copy of hard links in `<workdir>/stage`, so the single-app installer's icon is left out);
  `suite/mkicons.py` draws Bureau's own and places them.
- Last, the archive is unpacked into `<workdir>/bchk.*` and searched for certificate or key
  material (PEM armour, `*.crt`, `*.pem`, `*.key`, `*.p12`, `*.pfx`); if any is found the archive is
  renamed `.REFUSED` and the script fails. (Folio's and Dispatch's programs name "PRIVATE KEY" in
  their PEM readers; that alone is not material, and the check looks for the armour's `BEGIN` line.)

`source.sh` takes, per program, the files its checkout tracks in git (as they are in the working
tree), less `CLAUDE.md`, `.github/` and `.gitignore`; for Easel, as its own package does, only the
tests' `.c` and `.py` files and no `docs/`. Its key check refuses key files and PEM blocks with a
body; source that only names the armour (a parser, a test that makes keys at test time) is fine.

## The installer and the programs

The programs find the shared files with `core/respath.c` (in each repository): `PROGDIR:` first,
then `Bureau:`, then `FONTS:` for the screen fonts. `Install-Bureau` puts the screen fonts in
`FONTS:` (only those missing or older there), TrueType and Dict in the Bureau drawer, each program
in `Bureau:<App>`, and `Assign Bureau: <drawer>` in `S:User-Startup` with Installer's `startup`
(named "Bureau") and `makeassign`. Each program's own `tools/Install-<App>` installs into
`Bureau:<App>` when `Bureau:` is assigned and leaves out the drawers Bureau already has.

Checking the script on Linux: balanced parentheses, closed strings, known function names and the
arity of `if`/`set`/`foreach` can be checked by a small parser; Installer's behaviour itself needs an
Amiga or an emulator (Commodore's Installer 43.3 and AROS's both).
