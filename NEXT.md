# Quill: where the WordCraft gap work stopped, and what to do next

Stopped on 29 October 2026 at master `6e13e10`. Master builds clean: 1808 native tests pass (ASan/UBSan)
and the Amiga binary `Quill` links. The version string is still 0.14; nothing has been packaged since 0.14.

The full gap list, with every WordCraft command marked ✅/🟨/⬜/➖ and evidence, is
`docs/wordcraft-gap.md`. Ribbon commands: **432 ✅ · 4 🟨 · 65 ⬜ · 30 ➖ of 531** (Quill 0.14 had 236 ✅).
How work packages plug in (id blocks, wp_*.c files, hooks, lang rows, tests, file formats) is at the top
of `amiga/wphooks.h` and in "WP0 conventions" at the end of the gap document.

## Merged into master (done)

WP0 scaffolding · WP1 fields · WP5 comments and markup · WP8 proofing · WP9 character formatting ·
WP10 paragraph/Home commands · WP11 outline view · WP12 find/Go To/editing keys · WP13 styles and themes ·
WP14a equations · WP14b equation editor · WP15a charts/SmartArt kept and drawn · WP15b/c chart and SmartArt
editing · WP17 pictures · WP18 shapes and text boxes · WP19 WordArt and signature line · WP20 Draw tab ·
WP21a/b tables · WP22a pages and columns · WP22b note options · WP23 templates and building blocks ·
WP24 AutoSave/recovery/properties/accessibility · WP25 views and windows · WP28a RTF · WP28b ODT ·
WP28c HTML and Markdown · WP29 PDF depth and page images · WP31 proofing languages.

## WordCraft sync of 2026-11-18 (branch `wcsync`)

`docs/wordcraft-gap.md` was compared again with WordCraft's `main` at `22a73d9` (2026-11-17): twelve new ribbon
commands (Emphasis Mark, Fit One Page, the nine Paragraph Tools, Markdown Source) and View Side by Side /
Synchronous Scrolling, which WordCraft now has, plus nine section-2 features. Done on the branch: Paragraph Tools
(core/paratools.c, WP10), Fit One Page (core/fitpage.c, WP22a), Side by Side, Synchronous Scrolling and the
screen paper colours (WP25), removing recent documents (backstage.c), RTF comments (WP28a), line numbers in the
Line Number style. Still open from that report: Emphasis Mark (WP27), Markdown Source view, tracking style
changes (styles carry no tracked change in WP5's model), Delete Previous Sentence. Not checked on an Amiga yet:
the new ribbon items' fit at 640 px (Home's Paragraph group gained a list, View's Window group two buttons).

## Started but not finished (branches with work in progress)

Each is a branch `<name>` on GitHub (it was a git worktree in the original session); the last commit on each is a
"WIP" commit of whatever was on disk when work stopped. To continue: `git switch <name>`, `git merge master`,
build, finish the package as its entry in the gap document describes, test, merge into master.

| Branch | Package | Commits ahead | State |
|---|---|---|---|
| protect | WP6 Compare/Combine, WP7 Restrict Editing + encryption (core/cfb.c, core/crypt.c) | 15 | furthest along; needs finishing checks (LibreOffice/msoffcrypto opening encrypted files) |
| cite | WP3 citations and bibliography | 9 | merged into master (WP3 done: 16 citation styles, Source Manager, bibliography field) |
| refs | WP2 captions, table of figures, index, authorities, TOC as a field | 6 | merged into master (WP2 done: captions, table of figures, index, authorities, TOC field) |
| custom | WP26 Options panes, Customize Ribbon/QAT/Keyboard, command search, KeyTips, ribbon group collapsing at 640 px | 5 | partly built |
| controls | WP16 content controls, Developer tab, legacy form fields | 7 | merged into master |
| mailings | WP4 mail merge, envelopes, labels | 5 | merged into master |
| intl | WP27 right-to-left (bidi) and East Asian | 5 | finished on the branch (all WP27 lines ✅, see the gap document), ready to merge; ribbon fit at 640 px and drawing of Arabic/CJK not yet checked on an Amiga |

## Not started

- **WP28d .doc reader** (Word 97-2003): needs `core/cfb.c` from the `protect` branch first.
- **WP30 more interface languages** (Dutch, Swedish, Danish, Portuguese first; Latin-2 later): do it last,
  after all strings are in. Rows live in `tools/lang/wp*.py`; `tools/mklang.py` and `tools/langcheck.py`.

## Integration fixes to make before a release

1. **settings.xml element order**: WP5, WP7 and WP23 all write at hook position 1, so trackRevisions /
   documentProtection / attachedTemplate can come out of schema order; WP0's comment listing attachedTemplate
   at position 3 is itself against the schema. Write settings in one schema-ordered place.
2. **settings.xml.rels**: done on the `mailings` branch: packages add settings.xml's relationships with
   `docx_out_settings_rel` (core/docx.h) and docx_save writes the part once (WP23's attached template and WP4's
   data source both kept). That branch also writes WP23's slot-1 settings before WP4's and WP5's (schema order);
   WP7's documentProtection still needs checking when `protect` is merged.
3. **Ribbon width**: the Insert tab (and Home in German) is wider than 640 px; several packages put commands
   only in menus because of it. WP26's group collapsing (on the `custom` branch) is the fix.
4. **WordArt scheme colours** use the default Office theme; switch to the document's (`core/doctheme.h`).
5. **Gap document totals**: after each merge run `python3 tools/gapcount.py docs/wordcraft-gap.md --write`
   (or recount by hand); helpers only updated their own lines.
6. Then: README "New in 0.15", version strings (amiga/main.c, tools/Install-Quill), emulator check of the
   merged binary, package with the usual certificate check (search the package for certificate text and for certificate/key files; nothing from the sandbox CA may ship).

## Things learned (pitfalls)

- vbcc: `memchr` misses bytes above 0x7F passed as an int (use a loop or a `(char)` cast); inline
  `floor`/`ceil` with an FPU go through 32-bit integers; `%.*s` with precision 0 prints everything; -O2 has
  produced invalid `lea dN,aN` instructions (rewrite the loop). The full vbcc build now takes about 10 minutes.
- Quill is built without FPU code: doubles go through mathieeedoubbas.library (opened in
  amiga/wp_chartkeep.c). AROS's version compares large negative doubles wrongly.
- Private code points used as marks in the text: fields U+104000–U+10BFFF, equations U+10C000+, plus the older
  comment marks; writers for other formats must skip code points ≥ U+F0000 they don't model.
- Mask bits taken in CharFmt: 0x80, 0x10000000–0x40000000 (WP9), 0x04000000 CM_LANG, 0x02000000 CM_NOPROOF.
- The emulator (in the original session) was shared: always took a lock file first, keep sessions short; Alt key
  combinations don't reach the Amiga in this FS-UAE setup; Execute can't make temp files (put commands in the
  startup-sequence). Running ~15 helpers at once (each compiling with ASan) is likely what restarted the
  container: keep to about 8.
- Rule reminders: no `rm -f`/`rm -rf` (move aside), no printing, no ARexx/MCP scripting, no network features,
  menus alongside the ribbon, Quill's own icons, all strings translated (DE/FR/ES/IT), fit 640×480.
