# Quill: where the WordCraft gap work stopped, and what to do next

Stopped on 29 October 2026 at master `6e13e10`. Master builds clean: 1808 native tests pass (ASan/UBSan)
and the Amiga binary `Quill` links. The version string is still 0.14; nothing has been packaged since 0.14.

The full gap list, with every WordCraft command marked ✅/🟨/⬜/➖ and evidence, is
`docs/wordcraft-gap.md`. Ribbon commands: **432 ✅ · 4 🟨 · 65 ⬜ · 30 ➖ of 531** (Quill 0.14 had 236 ✅).
How work packages plug in (id blocks, wp_*.c files, hooks, lang rows, tests, file formats) is at the top
of `amiga/wphooks.h` and in "WP0 conventions" at the end of the gap document.

## Quill for the classic Mac OS (branch `mac-port`, 2026-11-18)

A Mac build: `make -f Makefile.mac` → `build/mac/Quill.bin`, with Riptide's tree beside Quill's (`../riptide`,
its `mac-port` branch: the MUI for the Mac). The core is unchanged; `mac/` is a small MUI front end of its own
(window, page view drawn with `pdf_page_rgb` and CopyBits at 1 to 32 bits, HIG menus, Standard File, Mac types
and creator `QUIL`, Finder icons) and a C library over the Toolbox. `docs/mac.md` tells the story, lists the
Command keys and what is not done (printing, more than one window, the ribbon's commands beyond the menus,
Apple events while running). Tested in the Aurora68 testbed at 1, 8 and 32 bits: open a .docx, type, bold,
save, close, reopen; the saved file checked on the host. Pitfalls found:

- `mac/libc.c` is the C library: keep it 32-bit clean in its own headers (`unsigned int` where the Mac's
  `long` is meant), so the host harness (core + mac/libc.c on Linux, see docs/mac.md "Tested") stays usable.
- A control moved or sized inside MUIM_Draw invalidates itself: the next update draws again, for ever.
- Save must FlushVol, or a crash (or the testbed stopping) loses the file's catalog entry.
- New Mac interface words go in `tools/lang/mac.py` (mklang reads it after the work packages).

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

## Last gaps (branch `gaps2`, 2026-11-18)

WP28e closed the last ribbon command and most cheap section-2 gaps: View › Markdown Source (core/markdown.c
`md_source_doc`/`md_apply_source`, amiga/wp_html.c, main.c `wp_swap_doc`), Insert › Shapes › New Drawing Canvas
(WP20's canvas was there already), Word XML (core/wordxml.c: Flat OPC read and written, Word 2003 XML read, both
through the .docx code), plain text encodings (UTF-8/UTF-16/Windows-1252 detected and kept, UTF-8 by default;
Save As Text asks), Strict Open XML measures (`docx_num`, tools/mkstrict.py), .docm with its VBA project kept
for macro-enabled files only, the .doc reader's bookmarks and comments, Delete Previous Sentence, Options'
initials and Read Aloud voice/speed/pitch. Tests: tests/test_wp28e.c. Ribbon: 512 ✅ · 3 🟨 · 0 ⬜ · 28 ➖.
Not checked on an Amiga: the View tab's new Source group at 640 px (ribbonfit says it fits), Save As Text's
encoding requester, Markdown Source switching, Read Aloud's voice settings (no narrator.device in the emulator).
LibreOffice 24.2 can't open Flat OPC, so Quill's Flat OPC is only checked against the .docx it stands for.

## Emulator check of the merged interface (branch `emucheck`, 2026-11-18)

Everything merged since 0.15 was run for the first time, on FS-UAE (A4000/68040, uaegfx) with the AROS ROM
and its Zune, at 640×480 and on a 1024×768 public screen (a small test program opened it; Quill took
`PUBSCREEN=`), in English and German. Windows were opened with the `CMD=` start argument (the command id),
tabs and buttons with the mouse. Checked: every ribbon tab (English and German at 640, English at 1024), the
Mailings windows (recipients, new list, filter, address block, greeting, rules, match fields, wizard, insert
field, check errors, envelopes, labels, merge to document, preview of merged data from a CSV), References
(caption, AutoCaption, table of figures, mark entry, index, mark citation, authorities; Insert Citation,
Create Source, Source Manager), Compare/Combine, Restrict Editing pane, Protect Document, Encrypt, Options
(General, Proofing, Accessibility with Read Aloud's voice settings), Customize Ribbon, Quick Access
Toolbar, Keyboard, Tell Me, KeyTips, the Developer tab, Phonetic Guide, Two Lines in One, Asian Typography,
Document Grid, Right-to-Left and Asian Options, Asian/complex script fonts, Fit One Page, Markdown Source,
a paper colour, the recent list's x, and a right-to-left document (Hebrew, Arabic shaped, Japanese drawn
with IPAGothic, numbers and brackets in place). The menus: 805 entries, none over Intuition's limits
(`DEBUG=` now logs any). No Enforcer-style hits or GURUs besides the Caption crash below.

Fixed on the branch:
- Insert Caption crashed (bus error in muimaster): `tr_list` ran past two lists without a NULL.
- Six Mailings windows never opened on Zune (no Radio class there): `radio()` falls back to a cycle gadget;
  Labels also read its Print choice with the wrong attribute on MUI. Match Fields (29 rows in a Scrollgroup,
  which that Zune also lacks) is now two pairs to a row and fits 480 lines.
- Review showed two Comments and two Tracking groups: groups with one caption on a tab are joined
  (`put_group`; `tools/ribbonfit.py` models it and fails on two groups with one caption).
- Group captions cut short (Direction, Create, Table of Authorities...) take spare room when squeezed;
  ribbonfit lists those still cut (View's Source and Show, Insert's Header & Footer and WordArt at 640).
- German: Speech and Language were both "Sprache" (Speech is now "Sprachausgabe").
- Options' General pane put the mini toolbar's box above its first heading; Phonetic Guide's Remove was
  MUI's SimpleButton.

Not checked, or seen and left:
- Zune draws frame titles (Filter and Sort, Labels, Merge to New Document) faint, white on the frame: a
  Zune look; check on MUI 3.8.
- Save As Text's encoding requester, Paragraph Tools' list, content control properties and form field
  options (they need a selection or a control at the caret), Compare/Combine and Encrypt actually run,
  Read Aloud speaking (no narrator.device in the emulator), KeyTips with Alt (Alt doesn't reach the
  emulated Amiga), merging to PDF.
- Real hardware still needed: AmigaOS 3.1/3.2 with MUI 3.8 (radio buttons, scroll groups, frame titles as
  MUI draws them), speed of the merged binary on a real 68040, AGA screens, narrator.device.
- How to repeat it: `Quill <document> CMD=<id> DEBUG=<log>` runs one command once the document is open (ids
  are the package enums, from `ID_WP_FIRST_OF(i)` = 4000 + 64 × the package's index in `amiga/ids.h`);
  the log names any dialog whose window couldn't be made.

## Started but not finished (branches with work in progress)

Each is a branch `<name>` on GitHub (it was a git worktree in the original session); the last commit on each is a
"WIP" commit of whatever was on disk when work stopped. To continue: `git switch <name>`, `git merge master`,
build, finish the package as its entry in the gap document describes, test, merge into master.

| Branch | Package | Commits ahead | State |
|---|---|---|---|
| protect | WP6 Compare/Combine, WP7 Restrict Editing + encryption, WP28d .doc reader | 22 | finished, master merged in, ready to merge: encryption checked both ways with LibreOffice 24.2 and msoffcrypto-tool; Restrict Editing ✅ with WP16's forms (`sdt_form_at`, `sdt_forms_protected`) and sections' `w:formProt`; .doc read 🟨 (no pictures, comments, bookmarks) |
| cite | WP3 citations and bibliography | 9 | merged into master (WP3 done: 16 citation styles, Source Manager, bibliography field) |
| refs | WP2 captions, table of figures, index, authorities, TOC as a field | 6 | merged into master (WP2 done: captions, table of figures, index, authorities, TOC field) |
| custom | WP26 Options panes, Customize Ribbon/QAT/Keyboard, command search, KeyTips, ribbon group collapsing at 640 px | 9 | merged into master |
| controls | WP16 content controls, Developer tab, legacy form fields | 7 | merged into master |
| mailings | WP4 mail merge, envelopes, labels | 5 | merged into master |
| intl | WP27 right-to-left (bidi) and East Asian | 5 | finished on the branch (all WP27 lines ✅, see the gap document), ready to merge; ribbon fit at 640 px and drawing of Arabic/CJK not yet checked on an Amiga |

## Not started

- **WP30 more interface languages** (Dutch, Swedish, Danish, Portuguese first; Latin-2 later): do it last,
  after all strings are in. Rows live in `tools/lang/wp*.py`; `tools/mklang.py` and `tools/langcheck.py`.

## Integration fixes to make before a release

1. **settings.xml element order**: done. docx.c's `docx_write_settings_ext` is the one place; in slot 1 it writes WP23's attachedTemplate group, WP4's mailMerge, WP5's revisionView/doNotTrack*, then WP7's documentProtection, as CT_Settings orders them (tests/test_wp7.c `settings_order`).
2. **settings.xml.rels**: done on the `mailings` branch: packages add settings.xml's relationships with
   `docx_out_settings_rel` (core/docx.h) and docx_save writes the part once (WP23's attached template and WP4's
   data source both kept). That branch also writes WP23's slot-1 settings before WP4's and WP5's (schema order); `protect` added WP7's documentProtection to the same single writer (`docx_write_settings_ext`), tested by `settings_order`.
3. **Ribbon width**: fixed by WP26 (`custom` branch): groups collapse to one button from the right, so every tab
   fits 618 px (a 640 window) in all five languages; `python3 tools/ribbonfit.py` checks it after a package adds
   groups. Commands that packages put only in menus for lack of room can now go on the ribbon.
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
