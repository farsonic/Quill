# CLAUDE.md - working on Quill

Quill is a native word processor for AmigaOS 3.x (68040, MUI 3.8 or Zune), written in C. It reads and
writes Word .docx, exports PDF, draws TrueType fonts itself and follows WordCraft's (a Word-like
program's) ribbon layout. It is GPL-2.0-only (see `LICENSE`). This file is for anyone, person or agent,
picking the work up. Read it first, then `NEXT.md` (where work stopped) and `README.md` (what Quill does).

## Layout

| Path | What |
|---|---|
| `core/` | Portable C99: document model, edits/undo, layout, .docx/.odt/.rtf/.html/.md readers and writers, PDF export, TrueType/CFF rasteriser, spelling, charts, equations. No AmigaOS calls: everything here builds and is tested on Linux/macOS. |
| `amiga/` | The MUI front end: window, ribbon (`ribbon.c`, `ribbondef.h`), page view (`view.c`), panes, backstage, dialogs, one `wp_*.c` per work package. `amiga/wphooks.h` explains how work packages plug in. |
| `tests/` | Native tests: `coretest.c` plus one `test_wpN.c` per work package, and `tests/data/` documents. |
| `tools/` | Generators and checks: interface strings (`tools/lang/`, `mklang.py`, `langcheck.py`), screen fonts (`mkamigafonts.py`), icons, hyphenation, thesaurus, the Installer script `Install-Quill`, `gapcount.py`. |
| `fonts/`, `fonts-amiga/`, `dict/` | TrueType fonts (SIL OFL), Amiga screen fonts made from them, word list, thesaurus, hyphenation patterns (each with its licence). |
| `docs/wordcraft-gap.md` | Every WordCraft ribbon command marked done / partial / missing / not applicable, with evidence. The plan of record. |
| `NEXT.md` | Where work stopped, unfinished branches, integration fixes, pitfalls. Keep it current. |

## Build and test

Native tests (needs only a C compiler; this is what CI runs, and it must stay green):

    cc -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined \
       core/*.c tests/coretest.c tests/test_wp*.c -o /tmp/quill_coretest && /tmp/quill_coretest | tail -3

Expected: `1808 tests, 0 failed` (or more as tests are added). Run from the repository root (tests read
`tests/data/` and `fonts/`).

Amiga binary: `./build.sh` runs the native tests, then builds `Quill` with vbcc (about 10 minutes).
It needs, at these default paths (or set `VBCC`, `NDK`, `MUI`):

- **vbcc** 0.9h with vasm and vlink and the m68k-amigaos target, in `/opt/amiga/vbcc`
  (http://www.compilers.de/vbcc.html; build from source, install the `vbcc_target_m68k-amigaos` and
  `vbcc_unix_config` archives, `VBCC` pointing at it).
- **NDK 3.2** C headers in `/opt/amiga/ndk/Include_H` (Aminet `dev/misc/NDK3.2.lha`).
- **MUI 3.8 developer headers** in `/opt/amiga/mui/include` (Aminet `dev/mui/mui38dev.lha`, its
  `C/Include`; it may only be redistributed whole, so it is not in this repository).

Packaging: `README.md` "Building" and `tools/Install-Quill` (the Installer script). Packages are `.lha`
archives; they are not committed (attach them to GitHub releases). Before any package ships, search it for
certificate or key material (`BEGIN CERTIFICATE`, `PRIVATE KEY`, `*.crt`, `*.pem`, `*.key`) and refuse.

Testing on an Amiga: FS-UAE with the AROS ROM and Zune works for most checks; real AmigaOS 3.1/3.2 with
MUI is the reference. Alt-key combinations may not reach an emulated Amiga through some FS-UAE setups.

## Rules of the project

- C99 that vbcc compiles: see the vbcc pitfalls in `NEXT.md` (`memchr` with bytes > 0x7F, `%.*s` with
  precision 0, `-O2` miscompiles). No floating-point library linked on the Amiga side: doubles go through
  mathieeedoubbas.library.
- Every interface string goes through `tools/lang/` with English, German, French, Spanish and Italian rows;
  run `python3 tools/langcheck.py` after adding strings. The window must fit 640x480.
- Shared files (screen fonts, TrueType, Dict) are found through `core/respath.c` (`PROGDIR:` first, then
  `Bureau:`, then `FONTS:` for screen fonts; the dirs are resolved once at startup into `amiga/resdirs.h`'s
  `dir_fonts`/`dir_truetype`/`dir_dict`): never hard-code `PROGDIR:Fonts`, `PROGDIR:TrueType` or
  `PROGDIR:Dict`. `core/respath.c/.h` are the same file in all five Bureau programs; change them everywhere.
- Menus alongside the ribbon for every command; Quill's own icons; no network features; no printing to
  real printers from tests.
- A new feature gets a native test in `tests/test_wpN.c`, and a line in `docs/wordcraft-gap.md` updated
  with evidence (then `python3 tools/gapcount.py docs/wordcraft-gap.md --write`).
- Don't delete with `rm -rf` in scripts or by hand on shared machines; move things aside.
- Commit identity: commits so far use `Claude <noreply@anthropic.com>` or `Quill <quill@localhost>`.
  Never put a personal email address in commits, code or docs.

## Where work stands

Every work package is merged into `master` (the seven branches that were unfinished, `protect`, `cite`, `refs`,
`custom`, `controls`, `mailings` and `intl`, plus the 2026-11 WordCraft sync and `gaps2`). Against WordCraft
`22a73d9` (2026-11-17), `docs/wordcraft-gap.md` counts 512 of 543 ribbon commands done, 3 partly, none missing
and 28 left out on purpose. Before the next release (0.16): an emulator and real-Amiga check of everything added
since 0.15 (new tabs and windows, ribbon collapsing at 640 px, right-to-left text), README "New in 0.16", version
strings, package with the certificate check. Still open: WP30 (more interface languages) and the partial items in
the gap document. Next upstream review: WordCraft commits after `22a73d9`.

## Issues

Bugs and requests come in as GitHub issues (templates in `.github/ISSUE_TEMPLATE/`). To work on them:

1. `gh issue list --repo farsonic/Quill` and `gh issue view N --repo farsonic/Quill --comments`.
2. Reproduce first. Most document bugs can be reproduced natively: load the attached file with the core
   (see `tests/qdump.c` / `tests/fmtconv.c`), write a failing test in `tests/`, then fix it in `core/`.
   Interface bugs need the Amiga build; say what you could and couldn't verify.
3. Fix on a branch named `issue-N-short-name`, keep the native tests green, and open a pull request whose
   description says `Fixes #N`, what changed, and how it was tested. Comment on the issue if more
   information is needed rather than guessing. Don't close issues by hand unless asked; merging the PR
   closes them.
4. Attached documents may be private: don't copy them into the repository unless the reporter agrees;
   make a minimal test file of your own instead.

`.github/workflows/claude.yml` lets people mention `@claude` in an issue or pull request to have Claude
Code look at it (it needs the `ANTHROPIC_API_KEY` repository secret or the Claude GitHub App).
