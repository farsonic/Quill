# Quill against WordCraft: what is missing

WordCraft (github.com/storytold/wordcraft, the Rust word processor, MIT OR Apache-2.0) is the program Quill follows. This list was made on 2026-10-29 from WordCraft's latest source: its feature catalogue (crates/engine/src/catalog.rs, the 520 ribbon and menu commands its `cargo xtask parity` checks), the ribbon itself (crates/ui-egui/src/ribbon/, which adds the Zotero and Help tabs: 11 more), its commands (crates/engine/src/cmd), its options, key bindings and languages, and its docs (file-format-parity.md, layout-parity.md, typing-parity.md, localization-parity.md, target-app-parity.md, gaps.md, parity-checklist.md). Quill 0.14 was checked command by command in its source: every ✅ cites the id (amiga/ids.h) or function that does it; a ⬜ was grepped for and not found.

Marks: ✅ Quill has it · 🟨 partly (what is missing is said) · ⬜ missing · ➖ left out on purpose (printing, as Quill exports PDF; ARexx/MCP/control protocols and macros; anything needing a network or cloud service), or WordCraft doesn't have it either (its catalogue lists 15 commands it hasn't built; they are ➖ here too). `[WPn]` after a missing item names the work package below that closes it.

## Summary

- **Ribbon commands:** 531 in WordCraft (the 520 of its catalogue plus Zotero and Help), 501 of them applicable to Quill. Quill has **432** (✅), **4** partly (🟨) and lacks **65** (⬜); 30 are ➖. So Quill covers 86% of the applicable commands fully and 87% at least in part.
- **Beyond the ribbon (section 2):** 152 features: ✅ 55, 🟨 43, ⬜ 37, ➖ 17.
- **Strongest:** Home (formatting, lists, styles), Table Layout, Header & Footer, Layout, Design, the views and the core editing keys.
- **Missing whole:** Mailings (22), Developer/content controls (11), Draw/ink (10), SmartArt (8) and charts (6), and almost all of References beyond the TOC and notes (citations, captions, index, authorities).
- **Biggest risks for documents people send:** fields other than page numbers and cross-references lose their codes on save; content controls, charts, SmartArt, equations, OLE objects and VML pictures are dropped (formatting revisions are kept since WP5); no AutoSave or recovery (WP24 adds them). These are in priority 1 below.
- **Other formats:** Quill reads and writes .docx and plain text and exports PDF; WordCraft also reads .doc and reads/writes .rtf, .odt, .html, .md, .tex and Word XML (WP28a-d).

| Tab | Commands | ✅ | 🟨 | ⬜ | ➖ |
|---|---|---|---|---|---|
| File | 23 | 18 | 1 | 2 | 2 |
| Quick Access | 5 | 5 | 0 | 0 | 0 |
| Title Bar | 1 | 0 | 0 | 1 | 0 |
| Home | 105 | 99 | 1 | 4 | 1 |
| Insert | 53 | 47 | 0 | 0 | 6 |
| Draw | 11 | 9 | 0 | 1 | 1 |
| Design | 10 | 10 | 0 | 0 | 0 |
| Layout | 22 | 19 | 0 | 3 | 0 |
| References | 25 | 9 | 1 | 14 | 1 |
| Mailings | 23 | 0 | 0 | 22 | 1 |
| Review | 31 | 25 | 1 | 3 | 2 |
| View | 29 | 25 | 0 | 0 | 4 |
| Read Mode | 3 | 2 | 0 | 0 | 1 |
| Developer | 11 | 0 | 0 | 11 | 0 |
| Zotero | 7 | 0 | 0 | 0 | 7 |
| Help | 4 | 1 | 0 | 0 | 3 |
| Table Design | 8 | 8 | 0 | 0 | 0 |
| Table Layout | 33 | 32 | 0 | 1 | 0 |
| Picture Format | 14 | 14 | 0 | 0 | 0 |
| Shape Format | 13 | 13 | 0 | 0 | 0 |
| Header & Footer | 5 | 5 | 0 | 0 | 0 |
| Equation | 22 | 21 | 0 | 0 | 1 |
| Outlining | 13 | 13 | 0 | 0 | 0 |
| SmartArt Design | 7 | 7 | 0 | 0 | 0 |
| Chart Design | 5 | 5 | 0 | 0 | 0 |
| Editing | 41 | 41 | 0 | 0 | 0 |
| Tools | 7 | 4 | 0 | 3 | 0 |
| **All** | **531** | **432** | **4** | **65** | **30** |

| Area | ✅ | 🟨 | ⬜ | ➖ |
|---|---|---|---|---|
| 2.1 File formats | 6 | 5 | 5 | 3 |
| 2.2 DOCX features | 9 | 15 | 13 | 1 |
| 2.3 Layout | 17 | 2 | 5 | 0 |
| 2.4 Editing | 11 | 11 | 5 | 7 |
| 2.5 Interface: panes, toolbars and the window | 10 | 2 | 5 | 2 |
| 2.6 The Options dialog | 1 | 5 | 2 | 1 |
| 2.7 Keyboard shortcuts | 1 | 0 | 2 | 0 |
| 2.8 Interface and proofing languages | 0 | 3 | 0 | 0 |
| 2.9 Zotero | 0 | 0 | 0 | 1 |
| 2.10 Control protocol, MCP, command line, scripting | 0 | 0 | 0 | 1 |
| 2.11 Network and cloud features (left out on purpose) | 0 | 0 | 0 | 1 |
| **All** | **55** | **43** | **37** | **17** |

## Work packages

### Avoiding conflicts

Nearly every package touches the same five files: amiga/ids.h (the command ids), amiga/ribbon.c (the tabs), amiga/main.c (`command()`, `ribbon_choice()`, the dialogs), amiga/lang_table.h (translations) and tests/coretest.c. Doing WP0 first lets the others run side by side:

- **WP0 Scaffolding** (1-2 h, do first). Touches amiga/ids.h, amiga/ribbon.c, amiga/main.c, amiga/lang_table.h, tools/mklang.py, build.sh, tests/coretest.c, core/docx.c (only `doc_load_file()`/`doc_save_file()`). Gives each package:
  - a block of 64 ids of its own in a new range from 4000 up (`ID_WP1_FIRST`…), clear of the notification ids at 1000 and the window ids at 3000 and 3100, so the existing range tests in main.c stay valid;
  - a hook in `command()` and `ribbon_choice()` sending its range to `wpN_command(id, value)` in a new file of its own (amiga/wp_fields.c and so on), added to build.sh;
  - empty tabs in ribbon.c for Mailings, Developer, Draw, and contextual Outlining, Equation, Chart Design, SmartArt Design (each package then fills only its own `Group` arrays), and a way to append groups to an existing tab from another file;
  - a per-package section marker in lang_table.h and a per-package test file (tests/test_wpN.c) called from coretest.c's `main()`;
  - a table of file formats in `doc_load_file()`/`doc_save_file()` and the open/save requesters, so WP28a-d each add one row.
- Packages that change the same core structures are put in an order below (the "after" notes). Inside core/doc.h, add new fields at the end of a struct.

### The packages

Sizes are for one engineer-agent; "ribbon" counts the ⬜ and 🟨 commands of section 1 the package closes. "After" means it builds on another package's work.

**Priority 1: documents people send, and everyday editing**

- **WP1 Fields** (ribbon 12; 4-6 h). A field model: a field's code and result kept as a range in the text (marks like the comment marks, a `Doc.fields` table); every field read from .docx kept with its code and cached result and written back (`w:fldChar`/`w:instrText`, `w:fldSimple`), so DATE, SEQ, MERGEFIELD, ADDIN (Zotero, Mendeley), IF, DOCPROPERTY, XE, TC survive a save; evaluate DATE, TIME, CREATEDATE, SAVEDATE, AUTHOR, TITLE, FILENAME, NUMPAGES, SECTION, SECTIONPAGES, DOCPROPERTY, SEQ; Insert › Field and Document Property windows; Date & Time with formats (as text or as a DATE field); Update Fields (F9), Show/Toggle Field Codes, Edit Field Code, Unlink, Lock, Unlock, Next/Previous Field; field shading. Files: core/doc.h, core/doc.c, new core/field.c/.h, core/docx.c, core/layout.c, amiga/view.c, new amiga/wp_fields.c, core/pdf.c (results only).
- **WP5 Review: comments and markup** (ribbon 6; 4-5 h). Comment replies and Resolve (`commentsExtended`, `commentsIds`, `commentsExtensible`, people.xml), a Comments pane with threads; Display for Review (Simple/All/No Markup/Original), Show Markup filters, balloons for deletions; Reviewing pane; Track Changes Options (colours by author, moves); formatting changes tracked and kept (`w:rPrChange`, `w:pPrChange`). Files: core/doc.h (Comment, CharFmt revision), core/doc.c, core/docx.c, amiga/view.c (balloons), amiga/panes.c, new amiga/wp_review.c.
- **WP9 Character formatting** (ribbon 9; 3-4 h). Hidden text (shown with marks, left out of PDFs), outline, shadow, emboss, engrave, underline colour, character shading kept apart from highlight (`w:shd` on runs), simple text effects; Enclose Characters if Amiga fonts allow; all in the Font window and .docx. Files: core/doc.h (CharFmt `fx` bits, `ulcolor`, `shade`), core/docx.c, core/layout.c, amiga/view.c (drawing), core/pdf.c, amiga/main.c (`open_font()`/`font_done()`), amiga/ribbon.c. **Done** (branch charpara): all 11 lines ✅ but Text Effects and Typography 🟨 (its fill/glow/reflection galleries are WP19's; those effects are kept verbatim meanwhile). Model: `CharFmt.fx2` (with `fx2mask` for changing single bits), `ulcolor`, `shade`, `rx` (kept run properties); core/charfx.c, core/docx_charfx.c, amiga/wp_charfmt.c, amiga/wp_charpara.h; tests in tests/test_wp9.c.
- **WP10 Paragraph and Home quick commands** (ribbon 16; 3-4 h; after WP9 for core/docx.c). Distributed alignment, Add Space Before / Remove Space After, Horizontal Line (a bottom border paragraph, as Word's AutoFormat makes), Hanging Indent and Remove Hanging (Ctrl+T), Define New Bullet (symbol and font) and Define New Number Format windows, a Sort Text window (descending, number/date, by field), nonbreaking space and hyphen and optional hyphen (keys, layout, marks, .docx `w:noBreakHyphen`/`w:softHyphen`), a Symbol window (a font's characters by code), a Paste Special window, Copy/Paste Formatting commands, the Clipboard pane. Files: amiga/main.c, amiga/ribbon.c, new amiga/wp_para.c, core/doc.h (`AL_DISTRIBUTE`), core/doc.c (sort), core/layout.c, core/docx.c, amiga/view.c (keys). **Done** (branch charpara): all 17 lines ✅. Core: `AL_DISTRIBUTE`, four more `NF_` formats, `doc_sort_text` (core/charfx.c), optional/nonbreaking hyphens in layout.c and docx.c; UI in amiga/wp_para.c (Sort Text, Symbol, Define New Bullet, Define New Number Format, Paste Special windows, the docked Clipboard pane, Ctrl keys); tests in tests/test_wp10.c.
- **WP12 Find, Go To, selection and editing keys** (ribbon 12; 4-5 h). Advanced Find: formatting and styles, special characters (^p ^t ^m ^l ^^), wildcards, find in the selection, Highlight All; Go To any target (page, section, line, bookmark, comment, footnote, endnote, field, table, picture, heading); Select Objects, Select Similar Formatting; Go Back (Shift+F5), Extend Selection (F8), column (block) selection, Spike and Insert Spike, Alt+X, Repeat (F4), Undo list. Files: core/doc.c (find), amiga/main.c (`open_find()`, `find_next()`, `open_goto()`), amiga/view.c (`key()`, selection), new amiga/wp_find.c.
- **WP14a Equations: model, layout, .docx** (ribbon 2; 5-7 h). Read and write OMML (`m:oMath`, `m:oMathPara`: fractions, scripts, radicals, n-ary operators, delimiters, functions, accents, limits, matrices, boxes, bars), lay them out with Quill's fonts (no OpenType MATH needed: build-up rules as in TeX), draw on the screen and in PDFs; Insert › Equation as linear format text. Files: new core/math.c/.h, core/docx.c, core/layout.c, amiga/view.c, core/pdf.c, new amiga/wp_math.c.
- **WP15a Charts and SmartArt: read, draw, keep** (ribbon 0, beyond-ribbon 2; 4-6 h). Keep chart and SmartArt parts (and their embedded workbooks) through a save, byte for byte with their relationships; draw charts from their cached data (bar, column, line, area, pie, doughnut, scatter; a placeholder with title for others) and SmartArt from its stored drawing (`dsp:drawing`); stop warning about them in `report_lost()`. Files: new core/chart.c, new core/smartart.c, core/docx.c, core/doc.h (`OBJ_CHART`, `OBJ_SMARTART`), core/layout.c, amiga/view.c, core/pdf.c.
- **WP16 Content controls** (ribbon 11; 4-5 h; after WP1 for the range marks). Keep `w:sdt` at block, inline, row and cell level (title, tag, lock, placeholder, every type, unknown `w:sdtPr` children verbatim); draw their bounds and titles; check boxes that toggle, drop-down and combo lists, date picker, picture control; Developer tab (11 commands, Design Mode, Properties window). Files: core/doc.h, core/doc.c, core/docx.c, amiga/view.c, new amiga/wp_controls.c, amiga/ribbon.c (its tab from WP0).
- **WP22b Footnote and endnote options** (ribbon 2; 2-3 h). The Footnote and Endnote window (position, number format, start, restart each page/section, per section), `w:footnotePr`/`w:endnotePr` read and written, endnotes at section ends, long footnotes continuing on the next page under a continuation separator, the document's own separators, Show Notes. Files: core/doc.h (SectFmt), core/docx.c, core/layout.c, new amiga/wp_notes.c.
- **WP24 Document safety and information** (ribbon 10; 4-5 h). AutoSave of saved documents (after a pause in typing), recovery copies offered at the next start (T: or a Quill drawer), local version history; editable properties (title, subject, author, keywords, description, custom properties: docProps/core.xml, app.xml, custom.xml); Inspect Document (find and remove comments, revisions, properties, hidden text); Check Compatibility; Accessibility Checker (missing alt text, heading order, table headers, contrast) with an Alt Text pane and the decorative mark. Files: new core/recover.c, new core/inspect.c, core/docx.c, amiga/main.c (timer in the input loop), amiga/backstage.c (Info page), new amiga/wp_info.c.
- **WP29 PDF export depth and page images** (ribbon 1; 3-4 h). Font subsetting (README roadmap), text outside Windows-1252 (Type 0/CID fonts with ToUnicode), internal links (TOC entries, cross-references, `#bookmark` links, note references), the document outline from headings, Export Image (PNG with the existing deflate in core/zip.c, and IFF ILBM). Files: core/pdf.c, core/pdf.h, new core/pngwrite.c, amiga/main.c (export), amiga/backstage.c (Export page).

**Priority 2: the rest of Word's writing tools**

- **WP2 Captions, tables of figures, index, authorities, custom TOC** (ribbon 11; 4-5 h; after WP1). Insert Caption (SEQ fields, labels, numbering with chapter numbers), Insert/Update Table of Figures, Mark Entry (XE), Insert/Update Index (columns, run-in, page ranges), Mark Citation (TA), Insert/Update Table of Authorities, a Custom Table of Contents window (levels, leaders, page numbers, from styles and TC fields); cross-references to captions and notes. Files: core/toc.c, new core/refs.c, core/docx.c (writing the fields), new amiga/wp_refs.c.
- **WP3 Citations and bibliography** (ribbon 4; 4-5 h; after WP1). Sources (Manage Sources, Create Source, Word's `b:Sources` part and Sources.xml), Insert Citation (CITATION fields), styles APA, MLA, Chicago, Harvard, IEEE (WordCraft has all 12 of Word's), Bibliography (BIBLIOGRAPHY field). Files: new core/cite.c, core/docx.c (customXml bibliography part), new amiga/wp_cite.c.
- **WP4 Mail merge, envelopes, labels** (ribbon 22; 6-8 h, may split into merge core and envelopes/labels). Recipients from CSV (and tab-separated; spreadsheets later), Edit/Filter/Sort Recipients, Insert Merge Field, Address Block, Greeting Line, Rules (IF, SKIPIF, NEXT, NEXTIF, ASK, FILLIN, SET), Match Fields, Highlight Merge Fields, Preview with first/previous/next/last/go to/find, Check for Errors, Finish & Merge to a new document or a PDF; Envelopes and Labels (common Avery sizes). Files: new core/merge.c, core/docx.c (`w:mailMerge` settings, MERGEFIELD through WP1), new amiga/wp_mailings.c, amiga/ribbon.c (its tab from WP0).
- **WP6 Compare and Combine** (ribbon 2; 3-4 h). A word-level diff of two documents into tracked changes, with the author named; Combine merges two reviewers' revisions. Files: new core/compare.c, new amiga/wp_compare.c.
- **WP7 Protection and encryption** (ribbon 3; 4-6 h). Restrict Editing (read only, comments only, tracked changes only, filling in forms, exceptions by `w:permStart`/`w:permEnd`, SHA-512 password hash) enforced in the edit paths; Protect Document / Encrypt with Password: ECMA-376 agile encryption (AES-256-CBC, SHA-512) in an OLE compound file, read and written. Files: new core/crypt.c, new core/cfb.c (compound file, shared with WP28d), core/docx.c, core/doc.c (edit checks), new amiga/wp_protect.c.
- **WP8 Proofing: grammar, thesaurus, AutoCorrect, AutoFormat, Read Aloud** (ribbon 4; 4-6 h). Grammar checks for English (repeated word, a/an, sentence capital, space before punctuation, double space), Spelling & Grammar window; a thesaurus from a word list with a compatible licence, also on the right-click menu; AutoCorrect with Options (replacement list, two initial capitals, sentence capitals, days, Caps Lock, exceptions); AutoFormat as you type (smart quotes, dashes, fractions, ordinals, `* `/`1. ` lists, `---` lines, links); Read Aloud through translator.library and narrator.device. Files: core/spell.c, new core/grammar.c, new core/autocorrect.c, new core/thesaurus.c, dict/ (data and licences), amiga/view.c (typing hook), new amiga/wp_proof.c. **Done** (branch proof): Spelling & Grammar, Thesaurus, AutoCorrect Options ✅, Read Aloud 🟨 (its speaking untested: no narrator.device in the test emulator); section 2's AutoCorrect, AutoFormat, Grammar, Thesaurus ✅. Core: core/grammar.c, core/thesaurus.c (dict/th_en.dat from WordNet, tools/mkthes.py), core/autocorrect.c; UI: amiga/wp_proof.c (+ wp_proof.h for view.c's grammar marks and typing hook, main.c's ID_SPELL); tests in tests/test_wp8.c.
- **WP11 Outline view and outline levels** (ribbon 19; 4-5 h). A paragraph outline level (`w:outlineLvl`, in the Paragraph window and styles), used by the navigation pane and the TOC; Outline view with the Outlining tab (promote, demote, to Heading 1, to body text, move up/down, expand, collapse, show level, show formatting, first line only, close); Alt+Shift+arrows to move paragraphs and promote/demote everywhere. Files: core/doc.h (ParaFmt), core/docx.c, core/toc.c, core/layout.c (`VM_OUTLINE`), amiga/view.c, amiga/panes.c, amiga/ribbon.c (its tab from WP0), new amiga/wp_outline.c.
- **WP13 Styles and themes** (ribbon 3; 3-4 h). Office themes: theme1.xml colours and fonts read, theme colours kept on runs and shapes (`w:themeColor`), the document's theme written back; Design › Effects; a docked Styles pane with previews; Style Inspector / Reveal Formatting; Modify Style with tab stops, borders and numbering (README roadmap); linked styles. Files: core/docx.c, core/doc.c, core/doc.h, amiga/main.c (`open_style()`), amiga/panes.c, new amiga/wp_styles.c.
- **WP17 Pictures** (ribbon 9; 4-5 h). Corrections (brightness, contrast, sharpen), Colour (greyscale, sepia, washout, recolour, saturation), Transparency, a few Artistic Effects, Picture Styles presets (frame, shadow, rounded, soft edge), Change Picture, Reset Picture, Compress Pictures, a simple Remove Background (by the corner colour, as Tight wrapping already finds); crop by dragging; GIF, BMP and IFF ILBM pictures; .docx `a:lum`, `a:grayscl`, `a:alphaModFix`, `a:duotone`, `a:clrChange`. Files: core/image.c, core/image.h, core/docx.c, core/pdf.c, amiga/view.c (crop handles), new amiga/wp_picture.c.
- **WP18 Shapes and text boxes** (ribbon 7; 4-5 h). Gradient and picture fills, shadow/glow/soft edge effects, a Format Shape pane (fill, line, effects, size, text box), text direction and vertical anchoring in text boxes (`bodyPr vert`, `anchor`), linked text boxes (`wps:linkedTxbx`, Create/Break Link), turned text in shapes (README roadmap), more preset shapes (callouts, flowchart, block arrows), Icons (a library drawn in code). Files: core/doc.h (Image), core/docx.c, core/layout.c (text box flow), amiga/view.c, core/pdf.c, new amiga/wp_shapes.c.
- **WP21a Table styles and quick tables** (ribbon 6; 3-4 h). New/Modify/Delete Table Style (README roadmap "a table style editor"), custom table styles written to .docx, Border Painter, Quick Tables (calendar, tabular list, matrix), Insert Spreadsheet (a table from CSV text). Files: core/tstyle.c, core/tstyle.h, core/docx.c, core/template.c, new amiga/wp_tables.c.
- **WP21b Table drawing and cell options** (ribbon 7; 4-5 h). Draw Table and Eraser with the mouse, Delete Cells (shift left/up), 9-way cell alignment, text direction in cells (README roadmap), per-cell margins, table alt text (`w:tblCaption`/`w:tblDescription`), floating tables with text round them (`w:tblpPr`). Files: core/table.c, core/doc.h (Cell, Table), core/docx.c, core/layout.c, amiga/view.c, new amiga/wp_tables2.c.
- **WP22a Pages, sections and columns** (ribbon 5; 4-5 h). Columns window (unequal widths, Left/Right presets, apply from here), balanced columns before a continuous break, per-section line numbers and page borders (README roadmap), page border sides and art-free styles, picture watermarks, Page Setup with gutter, mirror margins, 2 pages per sheet, vertical alignment Justified, hyphenation options (zone, caps, consecutive limit, manual hyphenation). Files: core/doc.h (SectFmt, PageBorder, Watermark), core/layout.c, core/docx.c, core/hyph.c, amiga/main.c (`open_pagesetup()`, `open_watermark()`), new amiga/wp_pages.c. **Done** (branch pages): new core/pages.c/.h, amiga/wp_pages.c/.h, tests/test_wp22a.c; the [WP22a] lines below say what each does. Left as they are: Word's art borders are drawn as a repeated pattern, not their pictures; balancing is for equal columns holding only lines; settings.xml's `w:mirrorMargins` and the like are written by docx_save itself right after `w:displayBackgroundShape` (before the packages' slot 1, as the schema orders them), and WP22a added slot 4 (after `w:evenAndOddHeaders`) for `w:bookFoldPrinting` and `w:printTwoOnOne`.
- **WP23 Templates, building blocks, inserting files** (ribbon 7; 3-4 h). .dotx/.dotm opened as templates (an untitled document), Save as Template, a personal templates folder on the File page's New list, Templates and Add-ins (attached template), Quick Parts and AutoText (the glossary document, `w:docPart`), Insert › Text from File / Object (another .docx or .txt at the caret). Files: core/template.c, core/docx.c, amiga/backstage.c, new amiga/wp_templates.c.
- **WP25 Views and windows** (ribbon 7; 3-4 h). New Window on the same document (a shared Doc with a reference count), Split, a Window menu listing open documents (README roadmap), Side to Side page movement, Multiple Pages with a choice of how many, a Zoom window, Read Mode options (column width, page colour sepia/inverse, text size, line spacing). Files: amiga/main.c (`win_new()`, DocWin), amiga/view.c, amiga/panes.c, new amiga/wp_windows.c.
- **WP28a RTF** (beyond-ribbon 1; 4-5 h). Read and write .rtf: text, character and paragraph formatting, styles, lists, tables, pictures, fields, code pages. Files: new core/rtf.c, a row in WP0's format table. **Done** (§2.1): core/rtf.c, core/codepage.c, core/fmtio.c (shared with WP28c), amiga/wp_rtf.c (File > Save As Rich Text...), tests/test_wp28a.c, tools/lo_formats.sh.
- **WP28c HTML and Markdown** (beyond-ribbon 2; 3-4 h). Read and write .html and .md (headings, lists, tables, links, pictures, bold/italic/code). Files: new core/html.c, new core/markdown.c, a row in the format table. **Done** (§2.1): core/html.c, core/markdown.c, amiga/wp_html.c (File > Save As Web Page..., Save As Markdown...), tests/test_wp28c.c.

**Priority 3: big or specialised**

- **WP14b Equation editor** (ribbon 20; 5-6 h; after WP14a). Editing equations in place, the Equation tab (structures gallery, symbols, Unicode/LaTeX linear input and Convert, Normal Text, inline/display, justification, equation numbers, structure commands). Files: new amiga/wp_equation.c, core/math.c, amiga/view.c, amiga/ribbon.c (its tab from WP0).
- **WP15b Insert Chart and Chart Design** (ribbon 6; 4-5 h; after WP15a). Insert Chart with a small data grid, chart types, title, legend, data labels, written as `c:chartSpace` with literal data. Files: core/chart.c, core/docx.c, new amiga/wp_chart.c, amiga/ribbon.c.
- **WP15c Insert SmartArt and SmartArt Design** (ribbon 8; 4-5 h; after WP15a). A few layouts (list, process, cycle, hierarchy), text pane, add shape, promote/demote, colours, reset, written with a drawing part Word can show. Files: core/smartart.c, core/docx.c, new amiga/wp_smartart.c, amiga/ribbon.c.
- **WP19 WordArt and signature line** (ribbon 6; 3-4 h; after WP9 and WP18). WordArt text effects (`w14:textFill`, `textOutline`, `shadow`, `glow`, `reflection`), Insert WordArt, Quick Styles, Text Fill, Text Outline, a few Transform warps; Signature Line. Files: core/doc.h, core/docx.c, amiga/view.c, core/pdf.c, new amiga/wp_wordart.c. **Done** (branch wordart): the 6 lines ✅. Model: the w14 effects stay in WP9's `CharFmt.rx` (one place, written once), parsed into a `TextFx` per rx number for drawing and rewritten part by part in the schema's order by `wa_set`; `Image.warp`, `Image.sig` (struct SigLine). Files: core/wordart.c/.h, core/sigline.c, amiga/wp_wordart.c/.h; small hooks in core/docx.c (signature line reader and writer, warp reader), core/objdocx.c (warp in bodyPr), core/pdf.c (WordArt runs and bent text), core/docx_charfx.c (w14:shadow no longer taken for w:shadow), core/doc.c (SigLine copied and freed), amiga/view.c (drawing). Tests in tests/test_wp19.c.
- **WP20 Draw tab and drawing canvas** (ribbon 12; 4-6 h). Pens, pencil, highlighter, eraser, lasso, Add Pen, Ink to Shape, Ink Replay, Hide Ink; ink saved as freeform shapes (`a:custGeom`, which also reads Word's freeforms); Drawing Canvas (`wpc:`, README roadmap). Files: core/doc.h (OBJ_INK, OBJ_CANVAS), core/docx.c, amiga/view.c (pen input), core/pdf.c, new amiga/wp_draw.c, amiga/ribbon.c (its tab from WP0). **Done** (branch draw): all 12 lines ✅. Model: `Image.ff` (a `Freeform`: paths, pen, drawing order, Word's own ink kept), `Image.canvas`, `SH_FREEFORM` (core/ink.h, core/ink.c): `a:custGeom` read (lines, Béziers, arcs, several paths each in its own space: Word's freeforms) and written; Word's ink (`w14:contentPart` in a drawing, a group or a canvas, its `mc:Fallback` picture left out) kept as written with its InkML part (kept by WP0's customXml keeping) and drawn from the part's traces (InkML differences decoded, brush colour and width); drawing canvases `wpc:wpc`. UI: amiga/wp_draw.c (Draw tab, Review › Ink, Insert and Review menu items, right-button items), amiga/wp_draw_view.h (included by view.c: drawing, eraser, lasso, replay). Ink to Text ➖: handwriting recognition needs a recogniser Quill doesn't have (and Word's uses a cloud service). Tests in tests/test_wp20.c.
- **WP26 Customization and discoverability** (ribbon 4; 4-5 h). Options in panes (General, Display, Proofing, Save, Language, Advanced) for the settings the other packages add; Customize Ribbon and the quick access buttons; Customize Keyboard (a key map in Quill.prefs); command search (Tell Me); KeyTips; a mini toolbar; the missing keyboard shortcuts of section 2.7 where Amiga keys allow. Files: amiga/ribbon.c, amiga/main.c, amiga/backstage.c, new amiga/wp_custom.c.
- **WP27 Right-to-left and East Asian** (ribbon 9; 8+ h, split into bidi and East Asian). UAX #9 bidi, right-to-left paragraphs, sections and tables, Arabic and Hebrew shaping, numerals; vertical text, document grid, Asian typography, ruby (Phonetic Guide). Needs fonts and an interface encoding beyond Latin-1; lowest priority for an Amiga word processor. Files: core/layout.c, core/doc.h, core/docx.c, core/fonts.c, core/ttraster.c, amiga/view.c.
- **WP28b ODT** (beyond-ribbon 1; 4-5 h). Read and write .odt (styles, lists, tables, pictures, headers and footers, notes). Files: new core/odt.c (core/zip.c already reads deflated entries), a row in the format table. **Done** (§2.1): core/odt.c (reading, rows `fmt_odt` and `fmt_fodt`), core/odtw.c (writing), core/odtx.h, core/mathml.c (MathML equations), amiga/wp_odt.c (File > Save As OpenDocument Text...), tests/test_wp28b.c, tests/data/lo-*.odt and lo-tables.fodt.
- **WP28d Word 97-2003 .doc reader** (beyond-ribbon 1; 8-12 h, two agents: text/formatting/styles, then tables/pictures/notes/fields). Files: new core/docbin.c, core/cfb.c (from WP7, or written here first).
- **WP30 More interface languages** (beyond-ribbon 1; 2-3 h for Latin-1 ones). Dutch, Swedish, Danish, Brazilian Portuguese first (Latin-1 fonts suffice); then Latin-2 screen fonts (tools/mkamigafonts.py) for Polish, Czech, Hungarian, Croatian, Slovenian, Romanian. Files: amiga/lang_table.h, amiga/lang.c, tools/mklang.py, tools/mkamigafonts.py, fonts-amiga/.
- **WP31 Proofing languages** (ribbon 2; 4-6 h). A language on each run (`w:lang`, read and written; Review › Language and Set Proofing Language), a Hunspell .aff/.dic reader so installed dictionaries work, more hyphenation patterns (German, Italian, Portuguese, Polish with compatible licences). Files: core/spell.c, core/hyph.c, core/doc.h (CharFmt.lang), core/docx.c, dict/, amiga/panes.c (the language on the status bar). **Done** (branch proof): Language and Set Proofing Language ✅. `CharFmt.lang`/`langea`/`langbidi`/`noproof` (CM_LANG 0x04000000, CM_NOPROOF 0x02000000, in CM_LOOK), core/proflang.c (languages, dictionaries per language, `proof_spell()`), core/hunspell.c (the .aff/.dic reader), core/docx_lang.c, hyphenation by language in core/hyph.c with dict/hyph_{de_DE,fr_FR,es_ES,it_IT,pt_PT,nl_NL,pl_PL}.dic (tools/mkhyph.py); UI amiga/wp_proflang.c, the status bar's language (panes.c); tests in tests/test_wp31.c.

### Order and parallel lanes

After WP0, these lanes don't share files (beyond their own blocks and new files):

1. WP1 → WP2 → WP3 → WP4 (fields, then everything built on fields)
2. WP5 → WP6 → WP7 (review, compare, protection)
3. WP9 → WP10 → WP19 (character, paragraph, WordArt)
4. WP12 → WP11 → WP25 (editing keys, outline, windows)
5. WP14a → WP14b (equations)
6. WP15a → WP15b → WP15c (charts, SmartArt)
7. WP16 (after WP1's marks), WP22b, WP22a
8. WP24, WP29, WP23
9. WP17, WP18, WP20 (objects; WP18 before WP19)
10. WP21a, WP21b
11. WP8, WP31, WP30, WP13
12. WP28a, WP28b, WP28c, WP28d
13. WP26, WP27 last.

core/docx.c is touched by most lanes. Each package should add its reading and writing in functions of its own (new `read_*`/`write_*` functions called from one line in the existing dispatch), which keeps merges mechanical.

## 1. Ribbon commands, tab by tab

In WordCraft's catalogue order (tab, then its groups), then the Zotero and Help tabs from its ribbon code. Each line: the mark, WordCraft's command (its group) and command id, Quill's evidence or what is missing, and the work package. WordCraft has no separate Header & Footer tab (those tools are in its catalogue as a pseudo-tab, shown here as such); Quill has Header & Footer and Edit Text contextual tabs of its own. Editing and Tools are WordCraft's keyboard and dialog commands that have no ribbon button.

### File (23: ✅ 18 · 🟨 1 · ⬜ 2 · ➖ 2)

- ✅ New (Backstage) `file.new` — `ID_NEW` (File menu Amiga-N, File page New)
- ✅ Open (Backstage) `file.open` — `ID_OPEN`
- ✅ Save (Backstage) `file.save` — `ID_SAVE` (also on the tab strip, `g_undo`)
- ✅ Save As (Backstage) `file.saveAs` — `ID_SAVEAS`; `ID_SAVETXT` for plain text
- ➖ Print (Backstage) `file.print` — printing left out on purpose (PDF export instead; Quill's own `ID_PRINT`/`print_doc()` is an untested printer.device dump)
- ➖ Print Markup (Backstage) `file.printMarkup` — printing left out on purpose
- ✅ Close (Backstage) `file.close` — `ID_CLOSE` (Amiga-W, `win_close_current()`)
- ✅ Info (Backstage) `file.info` — File page `BP_INFO` (backstage.c), filled by `ID_INFO` in main.c
- ✅ Properties (Backstage) `file.properties` — `ID_INFO_PROPS` (wp_info.c): Properties window (Summary, Statistics, Custom) from the Info page and File › Info; core.xml, app.xml, custom.xml read and written (core/info.c, tests/test_wp24.c `properties`) [WP24]
- ✅ Export PDF (Backstage) `file.exportPdf` — `ID_PDF` (Amiga-D, `export_pdf()`)
- ✅ Export Image (Backstage) `file.exportPng` — WP29: amiga/wp_pdf.c `export_image()` (File page › Export › Export Image, File menu), core/pngwrite.c `page_images_save()`: each page (or the Export Options range) as PNG (core/zip.c's deflate) or 24-plane IFF ILBM, 72-300 dpi, drawn by core/raster.c from the page's PDF content (compared side by side with poppler's rendering of the same PDF: the same; the 68040 build's PNG is byte-identical to the native one). Tests: tests/test_wp29.c [WP29]
- 🟨 Options (Backstage) `file.options` — `BP_OPTIONS` page: units (cm/in), theme, text size, language, user name, Window and screen; most of WordCraft's Options panes missing (see "Options") [WP26]
- ⬜ Numerals (Options) `view.numeral` — no Arabic/Hindi numeral choice [WP27]
- ✅ Share (Backstage) `file.share` — WordCraft's Share only opens Save As with a hint; Quill's Save As / Export cover it
- ⬜ Protect Document (Backstage) `file.protect` — no password encryption [WP7]
- ✅ Inspect Document (Backstage) `file.inspect` — `ID_INFO_INSPECT`: Document Inspector (comments, tracked changes, properties and personal information, headers/footers/watermarks, custom XML; Remove Checked), core/inspect.c; hidden text once WP9 models it [WP24]
- ✅ Check Accessibility (Backstage) `file.accessibility` — `ID_INFO_ACC`: Accessibility pane (missing alt text, heading order, table header row, contrast by WCAG ratio, floating objects, merged cells, empty paragraphs, link text, title), Go To / Alt Text / Mark as Decorative [WP24]
- ✅ Check Compatibility (Backstage) `file.compatibility` — `ID_INFO_COMPAT` (core/inspect.c `compat_check`: shapes, decorative marks, wide tables, notes beneath text, own separators, custom properties, kept parts) [WP24]
- ✅ Version History (Backstage) `file.versions` — `ID_INFO_VERSIONS`: each save kept in PROGDIR:Versions (a run of AutoSaves keeps its last, newest 25), Open Version / Restore / Delete (core/recover.c) [WP24]
- ✅ Recover Unsaved (Backstage) `file.recover` — `ID_INFO_RECOVER`: recovery copies every minute in PROGDIR:Recovery, offered at the next start and listed here (Open, Delete) [WP24]
- ✅ New from Template (Backstage) `file.newFromTemplate` — `ID_TEMPLATE` (5 built-in templates in core/template.c: Blank, Welcome, Letter, Résumé, Report)
- ✅ Save as Template (Backstage) `file.saveTemplate` — File › Save as Template… and the File page's Export › Save as Template (`ID_TP_SAVEAS`, wp_templates.c): a .dotx/.dotm with the template content type (core/blocks.c `template_save`, rows `fmt_dotx`/`fmt_dotm` in `doc_formats[]`), by default in PROGDIR:Templates; the document keeps its own file (a copy, as Save a Copy). Tests: tests/test_wp23.c [WP23]
- ✅ Personal Templates (Backstage) `file.listTemplates` — the File page's New list shows the templates in PROGDIR:Templates under the built-in ones (`personal_list()` in backstage.c, `wp23_personal()`); one opens as a new untitled document attached to it [WP23]

### Quick Access (5: ✅ 5 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Undo (Toolbar) `edit.undo` — `ID_UNDO` (Amiga-Z, tab strip button)
- ✅ Undo List (Toolbar) `edit.undoTo` — Edit › Find and Select › Undo List... / Redo List... (`ID_FX_UNDOLIST`, wp_find.c): the steps named by `doc_undo_step()` (Typing, Delete, Formatting, Replace All...), choosing one undoes it and those after it [WP12]
- ✅ Redo (Toolbar) `edit.redo` — `ID_REDO`
- ✅ Repeat (Toolbar) `edit.repeat` — F4 (`ID_FX_REPEAT`): the last command that made an undo step again (main.c `wp12_note`/`wp12_replay`, lists with their entry), or the text typed since it (`doc_undo_typed()`) [WP12]
- ✅ AutoSave (Toolbar) `file.autosave` — `ID_INFO_AUTOSAVE`: switch on the quick access bar, File › Info › AutoSave and File › Options; saves a document with a .docx of its own 3 s after typing stops (at most every 30 s) [WP24]

### Title Bar (1: ✅ 0 · 🟨 0 · ⬜ 1 · ➖ 0)

- ⬜ Search (Tell Me) (Search) `tools.search` — no command search [WP26]

### Home (105: ✅ 99 · 🟨 1 · ⬜ 4 · ➖ 1)

- ✅ Paste (Clipboard) `edit.paste` — `ID_PASTEDD` split button / `ID_PASTE`
- ✅ Paste Keep Text Only (Clipboard) `edit.pasteText` — `ID_PASTETEXT` (`CMD_PASTEAS`)
- ✅ Paste Merge Formatting (Clipboard) `edit.pasteMerge` — `ID_PASTEMERGE`
- ✅ Paste Special (Clipboard) `edit.pasteSpecial` — WP10's Paste Special window (`ID_P_PASTESPECIAL`, amiga/wp_para.c; Paste list "Paste Special...", Edit menu): Quill's formatted text, the destination's formatting, unformatted text, and RTF or HTML text on the clipboard read through the RTF/HTML formats' readers (WP28a/c rows of `doc_formats[]`; until they are merged it says no reader); Paste link ➖ (no linked objects on the Amiga), pictures ➖ (the clipboard device carries text only) [WP10]
- ✅ Cut (Clipboard) `edit.cut` — `ID_CUT`
- ✅ Copy (Clipboard) `edit.copy` — `ID_COPY`
- ✅ Format Painter (Clipboard) `edit.formatPainter` — `ID_BRUSH` (`CMD_BRUSH`)
- ✅ Copy Formatting (Clipboard) `edit.copyFormat` — `ID_P_COPYFMT` (Ctrl-Shift-C, Edit menu): the character formatting at the caret, and the paragraph's when the selection takes in a paragraph mark [WP10]
- ✅ Paste Formatting (Clipboard) `edit.pasteFormat` — `ID_P_PASTEFMT` (Ctrl-Shift-V, Edit menu), one undo step [WP10]
- ✅ Clipboard Pane (Clipboard) `edit.clipboardPane` — `ID_P_CLIPPANE` (Paste list "Clipboard Pane", Edit menu): docked right of the page, the last 24 things copied or cut in any window (view.c `copy_sel` hands them over), each pasted with its formatting; Paste All, Clear All, Delete [WP10]
- ✅ Font (Font) `format.font` — `ID_FONTDD` (font list with previews)
- ✅ Font Size (Font) `format.size` — `ID_SIZEDD`
- ✅ Increase Font Size (Font) `format.growFont` — `ID_GROW` (`CMD_GROW`)
- ✅ Decrease Font Size (Font) `format.shrinkFont` — `ID_SHRINK`
- ✅ Change Case (Font) `format.changeCase` — `ID_CASE` (5 kinds, `CMD_CASE`)
- ✅ Clear All Formatting (Font) `format.clear` — `ID_CLEARFMT`
- ✅ Bold (Font) `format.bold` — `ID_BOLD`
- ✅ Italic (Font) `format.italic` — `ID_ITALIC`
- ✅ Underline (Font) `format.underline` — `ID_UNDERLINEDD` (7 kinds, `UL_` in core/doc.h)
- ✅ Double Underline (Font) `format.doubleUnderline` — `ID_UNDERLINEDD` entry `UL_DOUBLE`
- ✅ Underline Color (Font) `format.underlineColor` — `CharFmt.ulcolor` (WP9): Font window "Underline colour", Text Effects list and Format › Text Effects › Underline Colour...; `w:u w:color` read and written; drawn on screen and in the PDF [WP9]
- ✅ Strikethrough (Font) `format.strikethrough` — `ID_STRIKE`
- ✅ Subscript (Font) `format.subscript` — `ID_SUB`
- ✅ Superscript (Font) `format.superscript` — `ID_SUPER`
- ✅ Text Effects Outline (Font) `format.outline` — `FX2_OUTLINE` (`w:outline`): Text Effects list, Font window, Format menu, right-button menu; hollow letters on screen, stroked (2 Tr) in the PDF [WP9]
- ✅ Text Effects Shadow (Font) `format.shadow` — `FX2_SHADOW` (`w:shadow`): a grey copy behind, on screen and in the PDF [WP9]
- 🟨 Text Effects and Typography (Font) `format.textEffects` — the Font group's Text Effects list (`ID_C_EFFECTS`): outline, shadow, emboss, engrave, hidden, shading, underline colour, enclose, standard ligatures and old-style/lining numbers (kept as `w14:ligatures`/`w14:numForm`, not drawn: the Amiga's fonts have no OpenType features). Every other `w14` text effect (fill, outline colour, glow, reflection, 3-D) and `w:effect`/`w:kern` are kept verbatim through a save (`CharFmt.rx`, core/charfx.c); their galleries are WordArt's, WP19, which draws fill, outline, shadow, glow, reflection and bevel (core/wordart.c) and edits them in place in `rx` [WP9]
- ✅ Text Highlight Color (Font) `format.highlight` — `ID_HIGHLIGHT` (16 colours)
- ✅ Font Color (Font) `format.color` — `ID_FONTCOLOR` (+ More Colours)
- ✅ Character Shading (Font) `format.shading` — `CharFmt.shade`, apart from the highlight (`w:shd` on runs, Word's pct patterns as greys): Text Effects list and Format menu (grey on or off, `ID_C_SHADETOGGLE`), Font window "Shading" colours; drawn under the highlight on screen and in the PDF [WP9]
- ✅ Font Dialog (Font) `format.fontDialog` — `ID_FONTDLG` (`open_font()`)
- ✅ Double Strikethrough (Font) `format.doubleStrikethrough` — Font window `fo_dstrike` (`FX_DSTRIKE`)
- ✅ Small Caps (Font) `format.smallCaps` — Font window `fo_small` (`FX_SMALLCAPS`)
- ✅ All Caps (Font) `format.allCaps` — `ID_CAPS`
- ✅ Hidden (Font) `format.hidden` — `FX2_HIDDEN` (`w:vanish`): Font window, Text Effects list, Format menu; takes no room unless formatting marks show (then a dotted underline: `Layout.showhidden`, layout.c `LAY_HIDDEN`), left out of the PDF [WP9]
- ✅ Emboss (Font) `format.emboss` — `FX2_EMBOSS` (`w:emboss`): white letters, a grey edge below right [WP9]
- ✅ Engrave (Font) `format.engrave` — `FX2_ENGRAVE` (`w:imprint`): white letters, a grey edge above left [WP9]
- ✅ Character Spacing (Font) `format.spacing` — Font window `fo_spc`/`fo_spcby` (`CharFmt.spacing`)
- ✅ Character Scale (Font) `format.scale` — Font window `fo_scale`
- ✅ Character Position (Font) `format.position` — Font window `fo_pos`/`fo_posby`
- ✅ Character Border (Font) `format.border` — `ID_CBORDER` (`FX_BORDER`)
- ⬜ Phonetic Guide (Font) `format.phonetic` — no ruby text (w:ruby is not read) [WP27]
- ✅ Enclose Characters (Font) `format.enclose` — the Enclose Characters window (`ID_C_ENCLOSE`; Text Effects list, Format menu): one or two characters in a circle, square, triangle or diamond, text shrunk or symbol enlarged; saved as Word's `eq \o\ac(○,字)` field with the symbol and text in runs of their own sizes, read back (also LibreOffice's), drawn on screen and in the PDF [WP9]
- ✅ Bullets (Paragraph) `para.bullets` — `ID_BULLETS`
- ✅ Numbering (Paragraph) `para.numbering` — `ID_NUMBERS`
- ✅ Multilevel List (Paragraph) `para.multilevel` — `ID_MULTILISTDD`
- ✅ Define New Multilevel List (Paragraph) `list.define` — `ID_LISTDEF` (`open_listdef()`)
- ✅ Multilevel List Library (Paragraph) `list.multilevelPreset` — `ID_MULTILISTDD` presets (`LP_` in doc.h)
- ✅ Change List Level (Paragraph) `para.listLevel` — "Next/Previous List Level" in `ID_MULTILISTDD`; Tab/Shift-Tab
- ✅ Restart Numbering (Paragraph) `para.restartNumbering` — "Restart at 1" (`CMD_LISTRESTART`)
- ✅ Define New Bullet (Paragraph) `para.defineBullet` — the Bullets split button's list (7 bullets, None, Define New Bullet...) and the Define New Bullet window (`ID_P_BULLETDEF`): a bullet from a list, by its code, or from the Symbol window, in a font of its own (written to numbering.xml for bullets now too); picture bullets ➖ (WordCraft's take a symbol and font only) [WP10]
- ✅ Define New Number Format (Paragraph) `para.defineNumber` — the Numbering split button's list and the Define New Number Format window (`ID_P_NUMDEF`): style (adds `NF_DECIMAL_ZERO` 01, `NF_ORDINAL` 1st, `NF_CARDINAL_TEXT` One, `NF_ORDINAL_TEXT` First to the roman, letter and decimal ones; core/charfx.c `nf_counter`), format text, start, font [WP10]
- ✅ Set Numbering Value (Paragraph) `para.setNumberingValue` — `ID_NUMVAL`
- ✅ Decrease Indent (Paragraph) `para.outdent` — `ID_OUTDENT`
- ✅ Increase Indent (Paragraph) `para.indent` — `ID_INDENT`
- ✅ Sort (Paragraph) `para.sort` — `ID_SORT` opens WP10's Sort Text window: up to three keys (paragraphs or field 1-9, separated by tabs, commas or a character), text/number/date, ascending or descending, header row, case sensitive (core/charfx.c `doc_sort_text`, one undo step); in a table, the rows by a column (`CMD_TSORT`) [WP10]
- ✅ Show/Hide Marks (Paragraph) `view.marks` — `ID_MARKS`
- ✅ Align Left (Paragraph) `para.alignLeft` — `ID_LEFT`
- ✅ Center (Paragraph) `para.alignCenter` — `ID_CENTER`
- ✅ Align Right (Paragraph) `para.alignRight` — `ID_RIGHT`
- ✅ Justify (Paragraph) `para.justify` — `ID_JUSTIFY`
- ✅ Distributed (Paragraph) `para.distribute` — `AL_DISTRIBUTE` (`w:jc distribute`): every line's characters spread between the margins, the last line's too (layout.c `wp10_distribute`), screen and PDF; Ctrl-Shift-J, Format › Paragraph Tools, the Paragraph window (no ribbon button: the Home tab is full at 640 pixels) [WP10]
- ✅ Line Spacing (Paragraph) `para.lineSpacing` — `ID_SPACINGDD` (1.0-3.0) and the Paragraph window
- ✅ Add Space Before (Paragraph) `para.addSpaceBefore` — `ID_P_SPACEBEFORE` (12 pt on or off, as WordCraft): Line Spacing list, Ctrl-0, Format › Paragraph Tools [WP10]
- ✅ Remove Space After (Paragraph) `para.removeSpaceAfter` — `ID_P_SPACEAFTER` (off, or 8 pt back): Line Spacing list, Format › Paragraph Tools [WP10]
- ✅ Shading (Paragraph) `para.shading` — `ID_PSHADEDD`
- ✅ Borders (Paragraph) `para.borders` — `ID_PBORDERDD`, Borders and Shading `ID_BORDERSDLG`
- ✅ Horizontal Line (Paragraph) `insert.horizontalLine` — `ID_P_HLINE`: a 1.5 pt bottom border, as Word's AutoFormat makes (Borders list, Insert menu) [WP10]
- ✅ Paragraph Dialog (Paragraph) `para.dialog` — `ID_PARADLG` (`open_para()`)
- ✅ Tabs (Paragraph) `para.tabs` — `ID_TABS` (`open_tabs()`; at most 8 stops a paragraph, `ParaFmt.tabs[8]`)
- ✅ Keep with Next (Paragraph) `para.keepNext` — Paragraph window `pa_keep`
- ✅ Keep Lines Together (Paragraph) `para.keepLines` — Paragraph window `pa_lines`
- ✅ Page Break Before (Paragraph) `para.pageBreakBefore` — Paragraph window `pa_break`
- ✅ Widow/Orphan Control (Paragraph) `para.widowControl` — Paragraph window `pa_widow`
- ✅ Outline Level (Paragraph) `para.outlineLevel` — `ParaFmt.outlvl` (`PM_OUTLINE`; "heading N" styles have N, core/doc.c), in the Paragraph and Modify Style windows (main.c `pa_olvl`, `sy_olvl`), `w:outlineLvl` read and written for paragraphs and styles (core/docx.c; body-text overrides of Heading styles kept); used by the navigation pane, the TOC (core/toc.c, as Word's `\o \u`) and the PDF outline; LibreOffice reads it (tests/test_wp11.c writes /tmp/quill_wp11.docx; its PDF bookmarks show the Normal paragraph with level 3) [WP11]
- ✅ Hanging Indent (Paragraph) `para.hangingIndent` — `ID_P_HANG` (Ctrl-T) and `ID_P_UNHANG` (Ctrl-Shift-T), Format › Paragraph Tools [WP10]
- ⬜ Right-to-Left Text Direction (Paragraph) `para.rtl` — no bidi [WP27]
- ⬜ Left-to-Right Text Direction (Paragraph) `para.ltr` — no bidi [WP27]
- ⬜ Asian Typography (Paragraph) `para.asianTypography` [WP27]
- ✅ Styles Gallery (Styles) `para.style` — `ID_STYLEPICK` (paragraph and character styles)
- ✅ Normal (Styles) `para.normal` — from the gallery (`ID_STYLEPICK`); no Ctrl+Shift+N key
- ✅ Heading 1 (Styles) `para.heading1` — from the gallery; no Ctrl+Alt+1 key
- ✅ Heading 2 (Styles) `para.heading2` — from the gallery; no Ctrl+Alt+2 key
- ✅ Heading 3 (Styles) `para.heading3` — from the gallery; no Ctrl+Alt+3 key
- ✅ Create a Style (Styles) `styles.create` — `ID_NEWSTYLE`
- ✅ Modify Style (Styles) `styles.modify` — `ID_MODSTYLE`/`ID_NEWSTYLE` open WP13's window (`wp13_style_dialog()`): name, type (paragraph, character, linked), based on, following; font, size, bold/italic/underline, colour (theme colours with shades), alignment, line spacing, spacing; Format pages Paragraph (indents, special, keep with next, keep lines, page break, widow control, contextual spacing), Tabs, Borders (sides, line, width, colour, spacing, shading), Numbering, Frame (`w:framePr`), Language (`w:lang`), Shortcut key (Ctrl+Alt/Ctrl+Shift+digit, `customizations.xml` wne:keymaps); Add to gallery, Automatically update, New documents based on this template (written into the attached template) [WP13]
- ✅ Update Style to Match (Styles) `styles.updateToMatch` — `ID_STYLEUPDATE`
- ✅ Delete Style (Styles) `styles.delete` — `ID_STYLEDELETE`
- ✅ Styles Pane (Styles) `styles.pane` — Design › Style Tools › Styles Pane, Format › Styles and Themes, View › Styles Pane (`ID_SP_PANE`, wp_styles.c): docked at the right of the page in the window's work area, drawn by Quill; each style in its own font, size and colour (Show Preview), ¶/a/¶a kind marks, the caret's style marked, Clear All, Disable Linked Styles, New Style / Style Inspector / Manage Styles buttons, Options (show recommended / in use / in document / all; sort alphabetical / as recommended / font / based on / type; kept in the file as `w:stylePaneFormatFilter`/`w:stylePaneSortMethod`); click applies (a linked style's character half on part of a paragraph), double-click modifies, bubbles describe the style [WP13]
- ✅ Style Inspector (Styles) `styles.inspector` — `ID_SP_INSPECT`: the paragraph and character styles at the caret and the formatting added to each (Plus:), Reset to Normal, Clear paragraph formatting, Default Paragraph Font, Clear character formatting, New Style, Reveal Formatting (`ID_SP_REVEAL`: font, effects, paragraph, breaks, tabs, borders, section; Distinguish style source; theme colours named as Word names them) [WP13]
- ✅ Manage Styles (Styles) `styles.manage` — `ID_STYLESPANE` (main.c's Styles window) and WP13's Manage Styles (`ID_SP_MANAGE`): Edit (sort, recommended only, modify, delete, new, Import/Export = Organizer copying styles to and from another .docx/.dotx), Recommend (priority `w:uiPriority`, move, assign, show / hide until used / hide), Restrict (`w:locked`), Set Defaults (docDefaults font, size, spacing, line spacing through `doc_restyle`) [WP13]
- ✅ Apply Styles (Styles) `styles.apply` — `ID_STYLEAPPLY` in the Styles window (no type-a-name box)
- ✅ Character Style (Styles) `format.charStyle` — `CMD_CHARSTYLE` from the gallery
- ✅ Find (Editing) `edit.find` — `ID_FINDDD` / `ID_FIND` (`open_find()`)
- ✅ Find Next (Editing) `edit.findNext` — `ID_FINDNEXT` (Amiga-G)
- ✅ Find Previous (Editing) `edit.findPrevious` — `ID_FINDPREV` (Find window button, `FIND_BACK`)
- ✅ Advanced Find (Editing) `edit.advancedFind` — Find › Advanced Find... and Edit › Find and Select (`ID_FX_ADVFIND`, wp_find.c; engine core/findx.c): ^p ^t ^l ^m ^n ^b ^? ^# ^$ ^w ^^ ^+ ^= ^s ^~ ^- ^% ^v ^g ^f ^e ^a ^d and ^13/^0151/^u8212 codes; Word's wildcards (? * [ ] [! ] {n,m} @ < > ( ) \x, \1-\9 and ^& ^c in Replace with); match case, whole words, prefix, suffix, ignore punctuation and white space; Search All/Down/Up; Find in the current selection; Format.../Replace Format... (font, size, bold... small caps, highlight, colour, alignment, paragraph and character style; No Formatting); Special list; Highlight All / Clear Highlighting (drawn by the view, not saved). Tests: tests/test_wp12.c [WP12]
- ✅ Replace (Editing) `edit.replace` — `ID_REPLACE`, `ID_REPLACE1`
- ✅ Replace All (Editing) `edit.replaceAll` — `ID_REPLACEALL` (one undo step)
- ✅ Go To (Editing) `edit.goto` — F5 / `ID_GOTO` opens the Go To page (`fx_goto()`): page, section, line, bookmark (name or number), comment (number or reviewer's name), footnote, endnote, field, table, graphic, object, heading; n, +n, -n, Previous/Next. (Equation: WP14a's equations don't exist yet) [WP12]
- ✅ Select All (Editing) `select.all` — `ID_SELALL`
- ✅ Select Objects (Editing) `select.objects` — Select › Select Objects (`CMD_FV_OBJECTS`, wp_find_view.h): a mode where dragging round floating objects chooses them together (for grouping), Esc ends it [WP12]
- ✅ Select Similar Formatting (Editing) `select.similar` — Select › Select Similar Formatting (`fx_similar()`, `CMD_FV_SIMILAR`): every stretch formatted like the caret's is selected at once; formatting commands, Copy, Cut and Delete act on all of them [WP12]
- ➖ Dictate (Voice) `tools.dictate` — WordCraft doesn't have it either (cloud speech service)
- ✅ Editor (Editor) `review.editor` — WordCraft's Editor runs Spelling: Quill `ID_SPELL`

### Insert (53: ✅ 47 · 🟨 0 · ⬜ 0 · ➖ 6)

- ✅ Cover Page (Pages) `insert.coverPage` — `ID_COVERDD` (3 designs, remove)
- ✅ Blank Page (Pages) `insert.blankPage` — `ID_BLANKPAGE`
- ✅ Page Break (Pages) `insert.pageBreak` — `ID_PAGEBREAK`
- ✅ Insert Table (Tables) `insert.table` — `ID_TABLE` (`open_table()`)
- ✅ Draw Table (Tables) `table.draw` — WP21b: Insert › Tables › Draw Table, Table Layout › Cells, Table › Draw Table (wp_tables2.c `ID_TB_DRAWTABLE`, a mouse tool in view.c; Esc ends it): a box drawn outside tables makes a one-cell table that size, a line across or down cells splits them (core/tbldraw.c `tbl_draw_stroke`, core/table.c `tbl_hsplit`/`tbl_vsplit`; tests/test_wp21b.c "draw table") [WP21b]
- ✅ Convert Text to Table (Tables) `table.fromText` — `ID_TOTABLE`
- ✅ Excel Spreadsheet (Tables) `insert.spreadsheet` — WP21a: Insert › Tables › Spreadsheet, Insert › Spreadsheet (CSV)...: a CSV/TSV file as a table (comma, semicolon or tab, quoted fields, Latin-1 or UTF-8, numbers to the right, heading row repeats; core/tblquick.c `tbl_from_csv`, tests "spreadsheet") [WP21a]
- ✅ Quick Tables (Tables) `table.quick` — WP21a: Insert › Tables › Quick Tables (a window to choose from) and Insert › Quick Tables: Calendar 1-3 (this month; Sunday first; week numbers), Double Table, Matrix, Tabular List, With Subheads 1-2, words in the interface language (core/tblquick.c `tbl_quick`, tests "quick tables") [WP21a]
- ✅ Pictures (Illustrations) `insert.picture` — `ID_PICTURE` (PNG, JPEG only)
- ➖ Online Pictures (Illustrations) `insert.onlinePicture` — WordCraft doesn't have it either (online service)
- ✅ Shapes (Illustrations) `insert.shape` — `ID_SHAPESDD` (24 presets, `SH_` in doc.h)
- ✅ Icons (Illustrations) `insert.icon` — `ID_SH_ICONS` (amiga/wp_shapes.c): a window of 52 icons in six categories, drawn in Quill's own icon language (core/shapex.c), inserted as a shape in its own colour; saved as `a:custGeom` named "Icon: …" in `wp:docPr`, which LibreOffice and Word draw and Quill reads back as the icon [WP18]
- ➖ 3D Models (Illustrations) `insert.model3d` — WordCraft doesn't have it either
- ✅ SmartArt (Illustrations) `insert.smartArt` [WP15c] — Insert > Illustrations > SmartArt and Insert > SmartArt... (amiga/wp_smartart.c): Choose a SmartArt Graphic by category (list, process, cycle, hierarchy, relationship, matrix, pyramid; 21 layouts) with a preview and description; written as the five diagram parts with a `dsp:drawing` Quill lays out itself (core/smartedit.c). tests/test_wp15c.c; LibreOffice draws every layout
- ✅ Chart (Illustrations) `insert.chart` [WP15b] — Insert > Illustrations > Chart and Insert > Chart... (amiga/wp_chart.c): every kind core/chart.c draws (25) with a preview of Word's sample data; written as `c:chartSpace` (Excel 2016's kinds as `cx:chartSpace`) with the values cached and an embedded workbook the formulas point into, so Word's Edit Data works (core/chartedit.c). tests/test_wp15b.c; LibreOffice draws every `c:` kind
- ➖ Screenshot (Illustrations) `insert.screenshot` — WordCraft doesn't have it either
- ✅ Drawing Canvas (Illustrations) `insert.canvas` — `ID_DR_CANVAS` (Draw › Insert, Insert › Drawing Canvas): an empty canvas the column's width in line; a canvas is a group whose first object is its background (`Image.canvas`, core/ink.h), read and written as `wpc:wpc` with `wpc:bg`/`wpc:whole` and its shapes, pictures, groups (`wpg:wgp`) and ink; moves as one; shapes added from its right-button menu (Add Shape to Canvas), ink drawn on it goes into it [WP20]
- ➖ Get Add-ins (Add-ins) `addins.get` — WordCraft doesn't have it either (online store)
- ➖ Online Video (Media) `insert.video` — WordCraft doesn't have it either (online)
- ✅ Link (Links) `insert.link` — `ID_LINK` (`open_link()`)
- ✅ Remove Link (Links) `insert.removeLink` — `ID_UNLINK`
- ✅ Bookmark (Links) `insert.bookmark` — `ID_BOOKMARK` (`open_bookmarks()`)
- ✅ Cross-reference (Links) `insert.crossReference` — `ID_XREF` (bookmarks and headings: text or page; no captions, notes, numbered items)
- ✅ Comment (Comments) `review.newComment` — `ID_COMMENT`
- ✅ Header (Header & Footer) `insert.header` — `ID_HEADERDD`
- ✅ Footer (Header & Footer) `insert.footer` — `ID_FOOTERDD`
- ✅ Page Number (Header & Footer) `insert.pageNumber` — `ID_PAGENUMDD`
- ✅ Edit Header (Header & Footer) `insert.editHeader` — `ID_HEADERDD` "Edit Header", `ID_HEADFOOT` (Amiga-H)
- ✅ Edit Footer (Header & Footer) `insert.editFooter` — `ID_HFFOOTER`
- ✅ Remove Header (Header & Footer) `insert.removeHeader` — `remove_headfoot(0)`
- ✅ Remove Footer (Header & Footer) `insert.removeFooter` — `remove_headfoot(1)`
- ✅ Format Page Numbers (Header & Footer) `layout.pageNumberFormat` — `open_pgfmt()` / `ID_PGFMTGO`
- ✅ Close Header and Footer (Header & Footer) `insert.closeHeader` — `ID_STORYCLOSE` (Esc)
- ✅ Different First Page (Header & Footer) `layout.differentFirstPage` — `ID_HFFIRST`
- ✅ Different Odd & Even (Header & Footer) `layout.differentOddEven` — `ID_HFEVEN`
- ✅ Link to Previous (Header & Footer) `layout.linkToPrevious` — `ID_HFOWN` ("This Section's Own" is Link to Previous turned off)
- ✅ Text Box (Text) `insert.textBox` — `ID_TEXTBOX`
- ✅ Quick Parts (Text) `insert.quickParts` — References › Quick Parts and Insert › Quick Parts menu (`ID_TP_QUICK`; the Insert tab is full at 640 pixels): the Quick Parts gallery's blocks from the document's glossary (`w:docPart`, read into `Doc.gloss`, kept verbatim and written back, core/blocks.c), PROGDIR:Templates/Building Blocks.dotx and the attached template; AutoText, Document Property (a DOCPROPERTY, TITLE, AUTHOR... field through WP1's core/field.h), Building Blocks Organizer (insert, edit properties, delete, preview), Save Selection to Quick Part Gallery (name, gallery, category, description, save in, insert as content / paragraph / page). Pictures, notes and links are left out of blocks Quill makes. Tests: tests/test_wp23.c [WP23]
- ✅ AutoText (Text) `insert.autoText` — `ID_TP_AUTOTEXT`: the AutoText gallery (`autoTxt`) and Save Selection to AutoText Gallery; F3 replaces the word before the caret by the AutoText entry of that name [WP23]
- ✅ Document Property (Text) `insert.docProperty` — WP1: the Document Property list (References › Fields; the Insert tab is full at 640 pixels) and window (Insert menu) (`ID_FLD_DOCPROP`, `ID_FLD_PROPWIN`, amiga/wp_fields.c) put in a DOCPROPERTY field; Word's built-in names and the file's custom properties [WP1]
- ✅ Field (Text) `insert.field` — WP1: the Field window (`ID_FLD_INSERT`, Ctrl-F9): 63 of Word's fields by category with descriptions, argument, \\* format, \\@ date picture, Preserve formatting, and the code itself to type (nested fields as { }); core/field.c `field_insert()` [WP1]
- ✅ WordArt (Text) `insert.wordArt` — Insert › WordArt (ribbon and Insert menu): a window with the text to use and Quill's 12 WordArt styles, each drawn as it looks; the pick becomes a floating text box with no fill or line, centred, sized to the text, its run in the style (w14 effects in `CharFmt.rx`). Effects drawn from the glyph outlines on screen and in PDFs (core/wordart.c), read and written once through .docx (tests/test_wp19.c), LibreOffice opens the file [WP19]
- ✅ Drop Cap (Text) `insert.dropCap` — `ID_DROPCAPDD`, `ID_DROPCAPDLG`
- ✅ Signature Line (Text) `insert.signatureLine` — Insert › Signature Line: Word's setup window (suggested signer, title, e-mail, instructions, allow comments, show sign date); drawn as Word draws an unsigned line (a cross, the line, name and title under it) into a PNG picture 192 × 96 pt; written as Word's VML `w:pict` with `o:signatureline` and read back with its setup (core/sigline.c); right-click › Signature Setup... changes it; LibreOffice shows it as a signature line. Signing itself (certificates) is not done: digital signatures are dropped on save by design (WP0) [WP19]
- ✅ Date & Time (Text) `insert.dateTime` — WP1: the Date and Time window (`ID_FLD_DATETIME`): 16 formats with today's date in the interface's language, as text or (Update automatically) a DATE/TIME field with that picture; `ID_DATE` still types today's date at once [WP1]
- ✅ Object (Text) `insert.object` — `ID_TP_OBJECT`: Create from File puts a document's text and formatting (any `doc_formats[]` reader) or a PNG/JPEG picture at the caret; Link to file and Display as icon are greyed (no OLE: ➖) [WP23]
- ✅ Text from File (Text) `insert.textFromFile` — `ID_TP_TEXTFILE`: Insert File window (file name, Range = a bookmark), one undo step (core/blocks.c `doc_insert_file`, tables come in as their text as `doc_fragment` copies them) [WP23]
- ✅ Equation (Symbols) `insert.equation` — the Equation button of Insert › Symbols (and Alt+=) puts a new equation in place and the caret in it, the selected text built up into it (WP14b); Insert › Equation... (Insert menu, amiga/wp_math.c): the equation typed in linear format (UnicodeMath subset: a/b, x^2, x_i, √(…), ∑_(i=1)^n▒…, brackets, functions, accents, ■ matrices, █ arrays, □ ▭ ⟡ boxes, "text"; characters outside Latin-1 as \control words), a display equation when the paragraph is empty as in Word; with an equation selected (or "Edit Equation..." on the right-button menu) it is changed. Checked: tests/test_wp14a.c (linear format in and out), emulator screenshot of the dialog [WP14a]
- ➖ Ink Equation (Symbols) `insert.inkEquation` — WordCraft doesn't have it either
- ✅ Symbol (Symbols) `insert.symbol` — the Symbol list's "More Symbols..." and WP10's Symbol window (`ID_P_SYMBOL`, Insert menu): every Latin-1 character as a button (what the interface can show), any Unicode character by its code (hex or decimal), inserted in a font chosen, the recently used ones, and a Special Characters page (dashes, spaces, hyphens, quotes, ©, ®, ™...) [WP10]
- ✅ Nonbreaking Space (Symbols) `text.nbsp` — `ID_P_NBSP` (Ctrl-Shift-Space, Insert › Special Characters, the Symbol window), shown as ° with formatting marks [WP10]
- ✅ Nonbreaking Hyphen (Symbols) `text.nbHyphen` — U+2011 (`ID_P_NBHYPHEN`, Ctrl-Shift-minus): drawn and measured as a hyphen, never broken at (lines now break after ordinary hyphens), `w:noBreakHyphen` read and written [WP10]
- ✅ Optional Hyphen (Symbols) `text.optionalHyphen` — U+00AD (`ID_P_SOFTHYPHEN`, Ctrl-minus): no room and not drawn unless a line breaks there (then a hyphen), ¬ with formatting marks, `w:softHyphen` read and written [WP10]

### Draw (11: ✅ 9 · 🟨 0 · ⬜ 1 · ➖ 1)

- ✅ Select (Tools) `draw.select` — `ID_DR_SELECT` (or Esc): back to selecting text and objects [WP20]
- ✅ Lasso Select (Tools) `draw.lasso` — `ID_DR_LASSO`: a loop drawn on the page chooses the strokes most of whose points are in it (and shapes whose centre is), shown with dotted frames; dragging inside them moves them (one undo step), Del deletes them, Color recolours them, Convert makes shapes of them (`ink_in_lasso`, amiga/wp_draw_view.h) [WP20]
- ✅ Eraser (Tools) `draw.eraser` — `ID_DR_ERASER` split button: Stroke Eraser (whole strokes) and Point Eraser (the bits under it, strokes split: `ff_erase`), also on ink in canvases; one undo step per drag [WP20]
- ✅ Pen (Pens) `draw.pen` — black, red and blue pens (`ID_DR_PEN0-2`); drawing with the mouse on the page (Print Layout), the stroke shown as it goes, then smoothed (jitter averaged, Douglas-Peucker, Chaikin: `ink_smooth`) into an ink object floating on the page; Thickness (0.25-6 mm) and Color lists; drawn on the screen and in PDFs (`ff_pdf`); saved as `wps:wsp` with `a:custGeom`, named "Ink Pen 3 #12" (pen and drawing order, as WordCraft names ink), which LibreOffice and Word show as freeforms [WP20]
- ✅ Pencil (Pens) `draw.pencil` — `ID_DR_PEN3`, grey, "Ink Pencil" [WP20]
- ✅ Highlighter (Pens) `draw.highlighter` — `ID_DR_PEN4`: yellow, 12 pt, behind the text (so the text shows through on the Amiga's screen and in PDFs), written with `a:alpha` 50 % for Word and LibreOffice [WP20]
- ✅ Add Pen (Pens) `draw.addPen` — `ID_DR_ADDPEN`: a pen of your own like the one in use (up to 8), chosen from My Pens; Thickness and Color change it (for the session, not saved) [WP20]
- ✅ Ink to Shape (Convert) `draw.inkToShape` — `ID_DR_INKSHAPE` toggle (strokes drawn while on become shapes) and Convert (`ID_DR_CONVERT`, also on the right-button menu) for the strokes chosen: lines (snapped level or upright), arrows (drawn in one stroke, either end first), rectangles, squares, ellipses, circles, isosceles and right triangles, diamonds, other polygons of up to 8 corners as freeform shapes (`ink_recognise`, core/ink.c, after WordCraft's recogniser); scribbles stay ink [WP20]
- ➖ Ink to Math (Convert) `draw.inkToMath` — WordCraft doesn't have it either
- ⬜ Drawing Canvas (Insert) `insert.canvas` [WP20]
- ✅ Ink Replay (Replay) `draw.replay` — `ID_DR_REPLAY`: every stroke drawn again point by point in the order it was drawn (`Freeform.order`, saved in the name), strokes from other files first; a click stops it [WP20]

### Design (10: ✅ 10 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Themes (Document Formatting) `design.theme` — `ID_THEMEDD` (8 Quill themes setting style set, fonts and colours) and WP13's Office Themes (`ID_TH_THEMES`: Office, Office 2013-2022, Office 2007-2010 and other Office themes as theme1.xml parts; Reset to Theme from Template, Browse for Themes (.thmx/.docx/.dotx), Save Current Theme (.thmx); Format › Styles and Themes › Office Theme… window) [WP13]
- ✅ Style Set (Document Formatting) `design.styleSet` — `ID_STYLESETDD` (6)
- ✅ Colors (Document Formatting) `design.themeColors` — `ID_COLORSETDD` (8 Quill heading colour sets) and WP13's Theme Colors (`ID_TH_COLORS`): 21 Office colour schemes and Customize Colors (12 colours, a name); text, paragraph shading and borders, cells and shapes with theme colours follow [WP13]
- ✅ Fonts (Document Formatting) `design.themeFonts` — `ID_THEMEFONTDD` (6 pairs) and WP13's Theme Fonts (`ID_TH_FONTS`): 20 Office font schemes and Customize Fonts; text in the theme's heading or body font follows [WP13]
- ✅ Paragraph Spacing (Document Formatting) `design.paragraphSpacing` — `ID_PSPACINGDD` (6)
- ✅ Effects (Document Formatting) `design.effects` — Design › Office Theme › Effects (`ID_TH_EFFECTS`): 12 effect schemes written as the theme's `a:fmtScheme` (fills, lines, shadows, glow, reflection, soft edges, bevels); Quill doesn't draw theme effects itself [WP13]
- ✅ Set as Default (Document Formatting) `design.setDefault` — `ID_SETDEFAULT` (kept in Quill.prefs)
- ✅ Watermark (Page Background) `design.watermark` — `ID_WATERMARKDD`, WP22a's Printed Watermark (`ID_PG_WATERMARK`, amiga/wp_pages.c): a picture (PNG/JPEG, scale, washout: Word's `WordPictureWatermark` VML in the header, read and written) or words (font, colour, semitransparent, diagonal/horizontal); drawn on screen and in PDF (core/pages.c `pages_wm_box`, `pages_washout`) [WP22a]
- ✅ Page Color (Page Background) `design.pageColor` — `ID_PAGECOLORDD`
- ✅ Page Borders (Page Background) `design.pageBorders` — `ID_BORDERDD` and WP22a's Page Borders window (`ID_PG_BORDERS`): each side on or off with its own space, Word's 25 art-free line kinds and art borders (drawn as a pattern, kept by name), colour, width, measured from the text or the page's edge, whole document / this section / its first page only / all but its first, surround header and footer, in front or behind (core/pages.c `pages_border_rects`, `w:pgBorders` with `w:display`, `w:zOrder`) [WP22a]

### Layout (22: ✅ 19 · 🟨 0 · ⬜ 3 · ➖ 0)

- ✅ Margins (Page Setup) `layout.margins` — `ID_MARGINSDD` (5 presets + custom)
- ✅ Orientation (Page Setup) `layout.orientation` — `ID_ORIENTDD`
- ✅ Size (Page Setup) `layout.size` — `ID_PAPERDD` (9 sizes + more)
- ✅ Columns (Page Setup) `layout.columns` — `ID_COLUMNSDD` (One, Two, Three, Line Between, Left, Right, More Columns...) and WP22a's Columns window (`ID_PG_COLUMNS`): presets, 1-6 columns of their own widths and spacing (`w:cols w:equalWidth="0"` with `w:col`), line between, Apply to whole document / this section / this point forward / selected text; each line laid out as wide as its column; balanced before a continuous break (layout.c `balance_cols`) [WP22a]
- ✅ Breaks (Page Setup) `layout.break` — `ID_BREAKSDD` (page, column, text wrapping, 4 section kinds)
- ✅ Line Numbers (Page Setup) `layout.lineNumbers` — `ID_LINENUMDD`, `ID_LINENUMDLG` (WP22a: with Apply to whole document / this section / this point forward / selected text)
- ✅ Hyphenation (Page Setup) `layout.hyphenation` — `ID_HYPHENDD` (None, Automatic, Manual..., Hyphenation Options...): WP22a's options window (zone, hyphenate words in CAPS, limit consecutive hyphens; `w:hyphenationZone`, `w:doNotHyphenateCaps`, `w:consecutiveHyphenLimit`) and Manual Hyphenation (each word that could break at a line's end, the place chosen, an optional hyphen U+00AD inserted; laid out by WP10's optional hyphens) [WP22a]
- ✅ Page Setup Dialog (Page Setup) `layout.pageSetup` — `ID_PAGESETUPDLG` → WP22a's Page Setup (`ID_PG_SETUP`): Margins (gutter left/top, inside/outside, orientation, multiple pages: normal, mirror margins, 2 pages per sheet, book fold with sheets per booklet), Paper, Layout (section start, different odd/even and first page, header/footer distance, vertical alignment), Apply to whole document / this section / this point forward; laid out mirrored and halved (core/pages.c `pages_effective`) [WP22a]
- ✅ Vertical Alignment (Page Setup) `layout.verticalAlign` — `ID_PAGEALIGNDD` (top, centre, bottom, justified: WP22a, layout.c `justify_page`, `w:vAlign w:val="both"`)
- ⬜ Document Grid (Page Setup) `layout.documentGrid` [WP27]
- ⬜ Section Direction (Page Setup) `layout.sectionRtl` [WP27]
- ⬜ Text Direction (Page Setup) `layout.textDirection` [WP27]
- ✅ Indent (Paragraph) `para.indents` — `ID_INDENTL`, `ID_INDENTR`
- ✅ Spacing (Paragraph) `para.spacing` — `ID_SPACEB`, `ID_SPACEA`
- ✅ Position (Arrange) `arrange.position` — `ID_OBJPOSDD`
- ✅ Wrap Text (Arrange) `arrange.wrap` — `ID_OBJWRAPDD` (square, tight, top and bottom, behind, in front; no Through)
- ✅ Bring Forward (Arrange) `arrange.bringForward` — `ID_FORWARDDD` (`CMD_RESTACK`)
- ✅ Send Backward (Arrange) `arrange.sendBackward` — `ID_BACKWARDDD`
- ✅ Selection Pane (Arrange) `arrange.selectionPane` — `ID_SELPANE` (`open_selpane()`)
- ✅ Align (Arrange) `arrange.align` — `ID_ALIGNOBJDD` (`CMD_OBJALIGN`)
- ✅ Group (Arrange) `arrange.group` — `ID_GROUPDD`, `ID_GROUP`, `ID_UNGROUP`
- ✅ Rotate (Arrange) `arrange.rotate` — `ID_OBJROTDD`

### References (25: ✅ 9 · 🟨 1 · ⬜ 14 · ➖ 1)

- 🟨 Table of Contents (Table of Contents) `references.toc` — `ID_TOCDD`/`ID_TOC` (one built-in format, levels 1-3); no Custom Table of Contents dialog (levels, leaders, page numbers, styles) [WP2]
- ✅ Add Text (Table of Contents) `references.addText` — `ID_ADDTEXTDD` (`add_text()`)
- ⬜ Update Table (Table of Contents) `references.updateToc` — (Table of Authorities) [WP2]
- ✅ Remove Table of Contents (Table of Contents) `references.removeToc` — `ID_REMOVETOC`
- ✅ Insert Footnote (Footnotes) `references.footnote` — `ID_NOTE`
- ✅ Insert Endnote (Footnotes) `references.endnote` — `ID_ENDNOTE`
- ✅ Next Footnote (Footnotes) `references.nextFootnote` — `ID_NEXTNOTE`
- ✅ Show Notes (Footnotes) `references.notes` — `ID_NOTES_SHOW` (wp_notes.c): to the next footnote or endnote (asks which when both), and from a note back to its reference [WP22b]
- ✅ Footnote Options (Footnotes) `references.noteOptions` — `ID_NOTES_OPTIONS`: Footnote and Endnote window (position, number format, start, restart each section/page, whole document or this section, Insert/Apply); footnotePr/endnotePr in settings.xml and each sectPr read and written (core/notes.c, tests/test_wp22b.c) [WP22b]
- ➖ Researcher (Research) `references.researcher` — needs an online service (WordCraft's only reports that)
- ⬜ Insert Citation (Citations) `references.citation` [WP3]
- ⬜ Manage Sources (Citations) `references.sources` [WP3]
- ⬜ Style (Citations) `references.citationStyle` [WP3]
- ⬜ Bibliography (Citations) `references.bibliography` [WP3]
- ⬜ Insert Caption (Captions) `references.caption` [WP2]
- ⬜ Insert Table of Figures (Captions) `references.tableOfFigures` [WP2]
- ⬜ Update Table of Figures (Captions) `references.updateFigures` [WP2]
- ✅ Cross-reference (Captions) `insert.crossReference` — `ID_XREF`
- ⬜ Mark Entry (Index) `references.markEntry` [WP2]
- ⬜ Insert Index (Index) `references.index` [WP2]
- ⬜ Update Index (Index) `references.updateIndex` [WP2]
- ⬜ Mark Citation (Table of Authorities) `references.markCitation` [WP2]
- ⬜ Insert Table of Authorities (Table of Authorities) `references.tableOfAuthorities` [WP2]
- ⬜ Update Table (Table of Authorities) `references.updateAuthorities` — (Table of Authorities) [WP2]
- ✅ Update Fields (Fields) `references.updateFields` — WP1: `ID_FLD_UPDATE` (F9, References › Fields, the field menu): the selected fields, the one at the caret, or (nothing chosen) every field in the text, headers, footers, notes, comments and text boxes; core/field.c `field_update()`/`field_update_all()` evaluate DATE, TIME, CREATEDATE, SAVEDATE, PRINTDATE, EDITTIME, AUTHOR, TITLE, SUBJECT, KEYWORDS, COMMENTS, LASTSAVEDBY, TEMPLATE, REVNUM, FILENAME, NUMPAGES, NUMWORDS, NUMCHARS, PAGE, SECTION, SECTIONPAGES, PAGEREF, REF, SEQ (\\r \\c \\h \\s), DOCPROPERTY, INFO, MERGEFIELD, IF, QUOTE, SYMBOL, USERNAME, USERINITIALS with \\* \\# \\@; locked fields and the rest (ADDIN, INDEX, CITATION...) keep their results; one undo step; the TOC still through `ID_TOCUPDATE` [WP1]

### Mailings (23: ✅ 0 · 🟨 0 · ⬜ 22 · ➖ 1)

- ✅ Envelopes (Create) `mailings.envelopes` — WP4: `ID_ML_ENVELOPES` (Mailings › Create, File › Mailings): the Envelopes window (delivery address taken from the selection, return address from the user's name or omitted, 13 sizes: US 6 3/4, Monarch, 9, 10, 11, 12, DL, C4, C5, C6, C6/C5, B5, B6); New Document makes the envelope a landscape page of its own (core/envlabel.c `env_doc`), Add to Document puts it before the text as a section of its own with the envelope's page setup (`env_add_to_doc`, one undo step), as Word's layout; the feed and printing are ➖ (Quill makes PDFs) [WP4]
- ✅ Labels (Create) `mailings.labels` — WP4: `ID_ML_LABELS`: the Labels window (address, full page of the same label or a single label at row/column, Avery US Letter (5160, 5161, 5162, 5163, 5164, 5165, 5167, 5168, 5195, 5366, 5371, 5395, 8160), Avery A4/A5 (L7159, L7160, L7161, L7162, L7163, L7165, L7167, L7173, L7651, J8160, C32011) and three generic sizes, with their sheet, margins, pitch and size); a new document holding the labels as a table with gap columns and exact rows (`label_doc`); merge labels and Update Labels (`label_update`: the first label's contents copied to the others, each after a NEXT field) [WP4]
- ✅ Start Mail Merge (Start Mail Merge) `mailings.start` — WP4: `ID_ML_START` list and File › Mailings: Letters, Envelopes (Envelope Options, the main document becomes the envelope), Labels (Label Options, the main document becomes a page of merge labels), Directory (records one after another), Normal Word Document (the merge settings dropped, the fields kept) and the Step-by-Step Mail Merge Wizard (`ID_ML_WIZARD`, six steps: type, starting document, recipients, writing, preview with Exclude, completing); E-mail Messages ➖ (no e-mail) [WP4]
- ✅ Select Recipients (Start Mail Merge) `mailings.recipients` — WP4: Type a New List (an editor with Word's 13 columns, columns can be added; saved as CSV and linked) and Use an Existing List: CSV and tab- or semicolon-separated text (UTF-8, UTF-16 with BOM or Latin-1), .xlsx (first sheet: shared and inline strings, numbers, booleans, dates by their number formats; core/mergedata.c, written after Ledger's reader) and another .docx's first table; the path, query and Match Fields kept in settings.xml's `w:mailMerge` (core/mergedocx.c, data source through word/_rels/settings.xml.rels, read back and re-linked when the document is opened); Outlook contacts ➖ [WP4]
- ✅ Edit Recipient List (Start Mail Merge) `mailings.editRecipients` — WP4: the Mail Merge Recipients window (a list with check marks and the first seven columns; check/uncheck, all, none; Filter and Sort; Find Duplicates and Validate Addresses (name, street, town, postcode or country present), each offering to uncheck them; Find Recipient; Edit for text lists; Refresh) [WP4]
- ✅ Filter Recipients (Start Mail Merge) `mailings.filterRecipients` — WP4: Filter and Sort window, six conditions joined by And/Or (Equal to ... Does not contain, Is blank, Is not blank; numbers compared as numbers), written to `w:query` as SQL (`SELECT * FROM ... WHERE ...`) and read back (`merge_parse_query`) [WP4]
- ✅ Sort Recipients (Start Mail Merge) `mailings.sortRecipients` — WP4: three sort keys, ascending or descending (stable), kept as `ORDER BY` in the query [WP4]
- ✅ Highlight Merge Fields (Write & Insert Fields) `mailings.highlightFields` — WP4: `ID_ML_HIGHLIGHT`, the merge fields' results on grey (amiga/view.c `merge_shade`) [WP4]
- ✅ Address Block (Write & Insert Fields) `mailings.addressBlock` — WP4: the Insert Address Block window (Word's ten name formats, company, postal address, country never/always/only if different from, a preview of each recipient, Match Fields) writes Word's ADDRESSBLOCK code (\\f groups, \\c, \\e); its result worked out from the matched columns (`merge_address`) when previewing and merging [WP4]
- ✅ Greeting Line (Write & Insert Fields) `mailings.greetingLine` — WP4: the Insert Greeting Line window (Dear/To/none, seven name formats, comma/colon/none, the line for invalid names, preview) writes GREETINGLINE (`merge_greeting`) [WP4]
- ✅ Insert Merge Field (Write & Insert Fields) `mailings.insertField` — WP4: the ribbon's list of the recipient list's columns (Word's address fields while there is none) and the Insert Merge Field window (database or address fields); MERGEFIELD with Word's underscores for spaces [WP4]
- ✅ Rules (Write & Insert Fields) `mailings.rules` — WP4: the list and the Insert Word Field window: Ask, Fill-in (asked when merging, \\o once, \\d default), If...Then...Else, Merge Record #, Merge Sequence #, Next Record, Next Record If, Set Bookmark (REF or the bookmark's name shows it), Skip Record If; all worked out by `merge_build`/`merge_preview` (core/merge.c) [WP4]
- ✅ Match Fields (Write & Insert Fields) `mailings.matchFields` — WP4: Word's 30 address fields, each matched to a column (found by Word's and common names first), saved as `w:odso/w:fieldMapData` and read back [WP4]
- ✅ Preview Results (Preview Results) `mailings.preview` — WP4: `ID_ML_PREVIEW` puts the record's values in the merge fields' results (one undo step), and Word's «Name», «AddressBlock», «Next Record»... back when it is turned off; `w:viewMergedData` and `w:activeRecord` kept [WP4]
- ✅ Next Record (Preview Results) `mailings.next` — WP4: `ID_ML_NEXT` (checked records only) [WP4]
- ✅ Previous Record (Preview Results) `mailings.previous` — WP4: `ID_ML_PREV` [WP4]
- ✅ First Record (Preview Results) `mailings.first` — WP4: `ID_ML_FIRST` [WP4]
- ✅ Last Record (Preview Results) `mailings.last` — WP4: `ID_ML_LAST` [WP4]
- ✅ Go to Record (Preview Results) `mailings.goToRecord` — WP4: `ID_ML_GOTO`, a record by its number [WP4]
- ✅ Find Recipient (Preview Results) `mailings.findRecipient` — WP4: the Find Entry window (all fields or one column, Find Next, wrapping round; `merge_find`) [WP4]
- ✅ Check for Errors (Preview Results) `mailings.checkErrors` — WP4: Word's three choices (simulate and report in a new document; merge pausing at each error; merge and report); finds merge fields no column has, Address Block/Greeting Line without a matched name, no recipients, no merge fields, and what the simulated merge reports (`merge_check`) [WP4]
- ✅ Finish & Merge (Finish) `mailings.finish` — WP4: Edit Individual Documents (all, current record, from-to) makes a new document with each record from a new page (a section each, as Word; merge fields replaced by their text, other fields kept), Merge to PDF writes one PDF or one for each record named from a column (core/mergepdf.c); Print Documents and Send Email ➖ [WP4]
- ➖ Print Documents (Finish) `mailings.printDocuments` — printing left out on purpose (merging to a new document or PDF is part of Finish & Merge)

### Review (31: ✅ 25 · 🟨 1 · ⬜ 3 · ➖ 2)

- ✅ Spelling & Grammar (Proofing) `review.spelling` — `ID_SPELL`/F7: the Spelling and Grammar window (amiga/wp_proof.c `wp8_spelling()`): each word by its own language's dictionary, English grammar by core/grammar.c, Ignore Once/All, Add, Change, Change All, AutoCorrect, grammar and style switches [WP8]
- ✅ Thesaurus (Proofing) `review.thesaurus` — Review > Proofing and Shift-F7: the Thesaurus window (meanings, synonyms, look up, insert), core/thesaurus.c with dict/th_en.dat (WordNet); Synonyms on the right-click menu [WP8]
- ✅ Word Count (Proofing) `review.wordCount` — `ID_WORDCOUNT` (`word_count()`)
- 🟨 Read Aloud (Speech) `review.readAloud` — Review > Speech: translator.library + narrator.device, a sentence at a time with the sentence selected, click again to stop; a message when the Amiga hasn't them. The speaking itself is untested: the test emulator (AROS) has translator.library but no narrator.device [WP8]
- ✅ Check Accessibility (Accessibility) `file.accessibility` — Review › Accessibility, `ID_INFO_ACC` [WP24]
- ✅ Alt Text (Accessibility) `view.altTextPane` — `ID_INFO_ALTPANE` / `ID_ALTTEXT`: Alt Text pane following the selected object, Mark as decorative (`adec:decorative` read and written); table alt text is WP21b's (`table.altText`) [WP24]
- ➖ Translate (Language) `review.translate` — needs an online service; WordCraft doesn't have it either
- ✅ Language (Language) `review.language` — Review > Language: Set Proofing Language (the Language window, amiga/wp_proflang.c) and Do Not Check Spelling or Grammar; a language on each run (`CharFmt.lang`, `w:lang` read and written), Hunspell dictionaries from the Dict drawer [WP31]
- ✅ New Comment (Comments) `review.newComment` — `ID_COMMENT`
- ✅ Delete (Comments) `review.deleteComment` — `ID_DELCOMMENT`
- ✅ Previous (Comments) `review.previousComment` — `ID_PREVCOMMENT`
- ✅ Next (Comments) `review.nextComment` — `ID_NEXTCOMMENT`
- ✅ Show Comments (Comments) `review.comments` — `ID_SHOWCOMMENTS`
- ✅ Resolve (Comments) `review.resolveComment` — `ID_RV_RESOLVE` (amiga/wp_review.c, `rv_set_done()`): the whole thread, dimmed in its balloon and the Comments pane, read and written as `w15:done` in commentsExtended.xml (tests/test_wp5.c) [WP5]
- ✅ Reply (Comments) `review.reply` — `ID_RV_REPLY` (`rv_reply()`, the Reply window, the right-button menu, the Comments pane): replies drawn in their thread's balloon, kept with `w15:paraIdParent`, durable ids (commentsIds) and UTC times (commentsExtensible) [WP5]
- ✅ Track Changes (Tracking) `review.trackChanges` — `ID_TRACK`
- ✅ Display for Review (Tracking) `review.markup` — `ID_RV_MARKUP` (a field with Simple Markup, All Markup, No Markup, Original; Review menu): the layout hides deletions or insertions (`BX_REVHIDE`, `rv_show_run()`), Original shows the old formatting, Simple Markup red change bars [WP5]
- ✅ Show Markup (Tracking) `review.showMarkup` — `ID_RV_SHOWMARKUP`: comments, insertions and deletions, formatting (kept as `w:revisionView`), balloons (revisions in balloons, comments and formatting only, all inline), Specific People (a Reviewers window) [WP5]
- ✅ Reviewing Pane (Tracking) `review.changes` — `ID_RV_RPANE`: a window with a summary (revisions by kind, comments) and every change and comment, chosen to go to it, Accept and Reject [WP5]
- ✅ Track Changes Options (Tracking) `review.trackingOptions` — `ID_RV_OPTIONS`: insertion, deletion, formatting and move marks, colours by author or fixed, changed lines (left, right, outside), comment colours, Track moves and Track formatting (the document's `w:doNotTrackMoves`/`w:doNotTrackFormatting`), balloons; kept in PROGDIR:Quill.review [WP5]
- ✅ Accept (Changes) `review.accept` — `ID_ACCEPTDD` (`CMD_RESOLVE`)
- ✅ Reject (Changes) `review.reject` — `ID_REJECTDD`
- ✅ Accept All (Changes) `review.acceptAll` — `ID_ACCEPTDD` "Accept All Changes"
- ✅ Reject All (Changes) `review.rejectAll` — `ID_REJECTDD` "Reject All Changes"
- ✅ Previous Change (Changes) `review.previousChange` — `ID_PREVCHANGE`
- ✅ Next Change (Changes) `review.nextChange` — `ID_NEXTCHANGE`
- ⬜ Compare (Compare) `review.compare` [WP6]
- ⬜ Combine (Compare) `review.combine` [WP6]
- ➖ Block Authors (Protect) `review.blockAuthors` — WordCraft doesn't have it either (co-authoring service)
- ⬜ Restrict Editing (Protect) `review.restrict` [WP7]
- ✅ Hide Ink (Ink) `review.hideInk` — `ID_DR_HIDE` (Review › Ink, which only fits a window wider than 640 pixels, so also in Draw › Convert; Review menu) hides and shows all ink; Delete All Ink (`ID_DR_DELALL`) beside it [WP20]

### View (29: ✅ 25 · 🟨 0 · ⬜ 0 · ➖ 4)

- ✅ Read Mode (Views) `view.readMode` — `ID_VREAD`
- ✅ Print Layout (Views) `view.printLayout` — `ID_VPRINT`
- ✅ Web Layout (Views) `view.webLayout` — `ID_VWEB`
- ✅ Outline (Views) `view.outline` — `VM_OUTLINE` (`ID_OL_VIEW`, View tab, View menu): web layout with core/layout.c's outline mode (`Layout.outline`: indents by level, body text under its heading, outline symbols, collapsed parts hidden), the contextual Outlining tab (amiga/wp_outline.c, wp_outline_view.h) [WP11]
- ✅ Draft (Views) `view.draft` — `ID_VDRAFT`
- ✅ Focus (Immersive) `view.focus` — `ID_FOCUS` (status bar)
- ✅ Immersive Reader (Immersive) `view.immersive` — WordCraft's command toggles Read Mode: Quill `ID_VREAD` (no line focus, syllables, text spacing)
- ✅ Vertical (Page Movement) `view.vertical` — pages run down the window (the only page movement)
- ✅ Side to Side (Page Movement) `view.sideToSide` — `ID_WW_SIDE` (View tab's Page Movement, View menu > Pages): every page in one row a page high, the wheel and the sideways scroll bar turn them; **Vertical** back (amiga/wp_windows.c, wp_windows_view.h) [WP25]
- ✅ Ruler (Show) `view.ruler` — `ID_RULER`
- ✅ Gridlines (Show) `view.gridlines` — `ID_GRID`
- ✅ Navigation Pane (Show) `view.navigationPane` — `ID_OUTLINE` (headings, pages, results; panes.c)
- ✅ Zoom (Zoom) `view.zoom` — `ID_ZOOMDD` (list); the Zoom window `ID_WW_ZOOMDLG` (200%, 100%, 75%, page width, whole page, many pages, percent) [WP25]
- ✅ 100% (Zoom) `view.zoom100` — `ID_ZOOM100`, `ID_ZOOM100B`
- ✅ One Page (Zoom) `view.onePage` — `ID_ONEPAGE`
- ✅ Multiple Pages (Zoom) `view.multiplePages` — `ID_MULTIPAGE` two pages a row; **Many Pages** (`ID_WW_MANY`: 2, 3, 4 pages, 2 x 2, 2 x 3, 2 x 4, fitted in the window), also in the Zoom window [WP25]
- ✅ Page Width (Zoom) `view.pageWidth` — `ID_PAGEWIDTH`
- ✅ Zoom In (Zoom) `view.zoomIn` — status bar + (`ID_ZOOMSET`, panes.c)
- ✅ Zoom Out (Zoom) `view.zoomOut` — status bar - (`ID_ZOOMSET`)
- ✅ Switch Modes (Dark Mode) `view.darkMode` — `ID_DARKPAGE`
- ✅ New Window (Window) `view.newWindow` — `ID_WW_NEWWIN`: another window on the same Doc (main.c `wp_win_new_shared()`; the Doc is freed with the last window showing it, which alone asks to save; titles Doc:1, Doc:2; each view lays out again what another changed: `ww_sync`) [WP25]
- ✅ Arrange All (Window) `view.arrangeAll` — `ID_WW_ARRANGE`: the windows one above another down the screen, in a grid when too small (WordCraft doesn't have it) [WP25]
- ✅ Split (Window) `view.split` — `ID_WW_SPLIT`: a second pane with its own scroll bar over the window's own, a balance between; the pane clicked into is worked in (keys, ribbon, rulers); again: one pane [WP25]
- ➖ View Side by Side (Window) `view.sideBySide` — WordCraft doesn't have it either
- ➖ Synchronous Scrolling (Window) `view.syncScroll` — WordCraft doesn't have it either
- ✅ Switch Windows (Window) `view.switchWindows` — `ID_NEXTWIN` (Amiga-`) cycles; **Switch Windows** list on the View tab and View > Window lists the open windows, the one it is in ticked (`wp25_windows_changed()`) [WP25]
- ➖ Macros (Macros) `tools.macros` — macros and scripting left out on purpose
- ➖ Record Macro (Macros) `tools.recordMacro` — macros and scripting left out on purpose
- ✅ Properties (SharePoint) `file.properties` — as File › Properties: `ID_INFO_PROPS` [WP24]

### Read Mode (3: ✅ 2 · 🟨 0 · ⬜ 0 · ➖ 1)

- ✅ Column Width (View) `view.readColumnWidth` — `ID_WW_RCOL`: Narrow and Wide reflow Read Mode's text into a column (3.5 and 6.5 inches) in the middle of the window, Default shows the pages; also Text Size (smaller to largest) and Line Spacing (wider: `Layout.linepct`) (View tab's Read Mode group, View > Read Mode Options, Read Mode's right-button menu) [WP25]
- ✅ Page Color (View) `view.readPageColor` — `ID_WW_RCOLOR`: None, Sepia, Inverse (the dark page while reading) [WP25]
- ➖ Layout (View) `view.readLayout` — WordCraft doesn't have it either

### Developer (11: ✅ 0 · 🟨 0 · ⬜ 11 · ➖ 0)

- ⬜ Rich Text Content Control (Controls) `developer.richText` — content controls are dropped on reading (docx.c skips w:sdt, keeps the text) [WP16]
- ⬜ Plain Text Content Control (Controls) `developer.plainText` [WP16]
- ⬜ Picture Content Control (Controls) `developer.picture` [WP16]
- ⬜ Building Block Gallery Content Control (Controls) `developer.buildingBlock` [WP16]
- ⬜ Check Box Content Control (Controls) `developer.checkBox` [WP16]
- ⬜ Combo Box Content Control (Controls) `developer.comboBox` [WP16]
- ⬜ Drop-Down List Content Control (Controls) `developer.dropDown` [WP16]
- ⬜ Date Picker Content Control (Controls) `developer.datePicker` [WP16]
- ⬜ Repeating Section Content Control (Controls) `developer.repeatingSection` [WP16]
- ⬜ Design Mode (Controls) `developer.designMode` [WP16]
- ⬜ Properties (Controls) `developer.properties` [WP16]

### Zotero (7: ✅ 0 · 🟨 0 · ⬜ 0 · ➖ 7)

- ➖ Add/Edit Citation (Citations) `ui.zotero.addEditCitation` — Zotero needs the Zotero desktop app reached over its local HTTP connector; there is no Zotero for AmigaOS
- ➖ Add Note (Citations) `ui.zotero.addNote` — (as above)
- ➖ Move Past Citation (Citations) `caret.pastCitation` — (as above)
- ➖ Add/Edit Bibliography (Bibliography) `ui.zotero.addEditBibliography` — (as above)
- ➖ Refresh (Document) `ui.zotero.refresh` — (as above)
- ➖ Document Preferences (Document) `ui.zotero.setDocPrefs` — (as above)
- ➖ Unlink Citations (Document) `ui.zotero.removeCodes` — (as above; Zotero's ADDIN fields are now kept through a save by WP1, and Unlink Fields turns them into plain text)

### Help (4: ✅ 1 · 🟨 0 · ⬜ 0 · ➖ 3)

- ✅ Help (Help) `ui.dialog about` — `ID_ABOUT` (About, File page `BP_ABOUT`); bubble help on every tool
- ➖ Check for Updates (Help) `ui.checkForUpdates` — network
- ➖ Community Discord (Help) `ui.discord` — network (opens a web page)
- ➖ Commands (Agents) `ui.dialog commands` — WordCraft's list of command ids for agents/scripts: control protocols left out on purpose

### Table Design (8: ✅ 8 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Table Style Options (Table Style Options) `table.look` — `ID_LK_FIRSTROW` … `ID_LK_BANDCOLS` (6)
- ✅ Table Styles (Table Styles) `table.style` — `ID_TSTYLEDD`
- ✅ New Table Style (Table Styles) `table.newStyle` — WP21a: Table Design › Style › New Style, Table › Table Styles: the table style editor (name, based on, every part: whole table, header/total rows, first/last column, odd/even banded rows and columns, the four corner cells; bold, italic, underline, size, colour, shading, borders in any pen, 9-way alignment; a picture of the result); written to styles.xml as a `w:customStyle` table style with its `w:tblStylePr` parts (core/tstyle.c `tstyle_define`, tests "new table style") [WP21a]
- ✅ Modify Table Style (Table Styles) `table.modifyStyle` — WP21a: the editor on the caret's table's style; every table in it (or in styles based on it) is formatted afresh in one undo step (`tstyle_define`, tests "modify table style") [WP21a]
- ✅ Delete Table Style (Table Styles) `table.deleteStyle` — WP21a: the caret's table's style deleted after asking; its tables keep their look without a style, styles based on it take its base, it isn't saved (`tstyle_delete`, tests "delete table style") [WP21a]
- ✅ Shading (Table Styles) `table.shading` — `ID_SHADINGDD`
- ✅ Borders (Borders) `table.borders` — `ID_TBORDERSDD` with pen `ID_PENSTYLE`/`ID_PENWIDTH`/`ID_PENCOLOR`
- ✅ Border Painter (Borders) `table.borderPainter` — WP21a: Table Design › Paint › Border Painter, a mouse tool: a click puts the Borders group's pen (style, weight, colour) on the nearest cell edge (both cells' sides), a stroke on every edge along it; Esc ends it (core/tbldraw.c `tbl_paint_stroke`, tests "border painter") [WP21a]

### Table Layout (33: ✅ 32 · 🟨 0 · ⬜ 1 · ➖ 0)

- ✅ Select Table (Table) `table.selectTable` — `ID_TSELECTDD` (`CMD_TSELECT` 3)
- ✅ Select Row (Table) `table.selectRow` — `ID_TSELECTDD`
- ✅ Select Column (Table) `table.selectColumn` — `ID_TSELECTDD`
- ✅ Select Cell (Table) `table.selectCell` — `ID_TSELECTDD`
- ✅ View Gridlines (Table) `table.viewGridlines` — `ID_TGRID`
- ✅ Properties (Table) `table.properties` — `ID_TPROPS` (`open_tprops()`)
- ✅ Alt Text (Table) `table.altText` — WP21b: Table Layout › Cells › Alt Text, Table › Alt Text..., right-button menu: title and description read and written as `w:tblCaption`/`w:tblDescription` (tests "alt text and floating") [WP21b]
- ⬜ Table Direction (Table) `table.rtl` [WP27]
- ✅ Draw Table (Draw) `table.draw` — WP21b: Table Layout › Cells › Draw Table (see Insert) [WP21b]
- ✅ Eraser (Draw) `table.eraser` — WP21b: Table Layout › Cells › Eraser, Table › Eraser, a mouse tool: the edge between two cells nearest the click goes (the cells merged), an outside edge loses its line (core/tbldraw.c `tbl_erase_stroke`, tests "eraser") [WP21b]
- ✅ Delete Cells (Rows & Columns) `table.deleteCells` — WP21b: Table Layout › Cells › Delete Cells, Table › Delete Cells..., right-button menu: shift cells left (rows end short, as in Word), shift cells up (empty cells at the bottom), entire row, entire column, on the selected block (core/table.c `tbl_delete_cells`, tests "delete cells") [WP21b]
- ✅ Delete Rows (Rows & Columns) `table.deleteRow` — `ID_DEL_ROW`
- ✅ Delete Columns (Rows & Columns) `table.deleteColumn` — `ID_DEL_COL`
- ✅ Delete Table (Rows & Columns) `table.deleteTable` — `ID_DEL_TABLE`
- ✅ Insert Above (Rows & Columns) `table.insertRowAbove` — `ID_ROW_ABOVE`
- ✅ Insert Below (Rows & Columns) `table.insertRowBelow` — `ID_ROW_BELOW`
- ✅ Insert Left (Rows & Columns) `table.insertColumnLeft` — `ID_COL_LEFT`
- ✅ Insert Right (Rows & Columns) `table.insertColumnRight` — `ID_COL_RIGHT`
- ✅ Merge Cells (Merge) `table.merge` — `ID_MERGE`
- ✅ Split Cells (Merge) `table.split` — `ID_SPLIT`
- ✅ Split Table (Merge) `table.splitTable` — `ID_SPLITTABLE`
- ✅ AutoFit (Cell Size) `table.autofit` — `ID_AUTOFITDD`
- ✅ Row Height (Cell Size) `table.rowHeight` — `ID_ROWH`
- ✅ Column Width (Cell Size) `table.columnWidth` — `ID_COLW`
- ✅ Distribute Rows (Cell Size) `table.distributeRows` — `ID_DISTROWS`
- ✅ Distribute Columns (Cell Size) `table.distributeColumns` — `ID_DISTCOLS`
- ✅ Alignment (Alignment) `table.cellAlign` — `ID_VALIGN0`-`2`, and WP21b's 9-way Align list (Table Layout › Cells, Table › Cell Alignment, right-button menu) setting the cells' vertical alignment and their paragraphs' alignment (core/table.c `tbl_cell_align`, tests "cell align"); table styles can set it too (`w:vAlign` in a style's `w:tcPr`) [WP21b]
- ✅ Text Direction (Alignment) `table.textDirection` — WP21b: Table Layout › Cells › Text Direction, Table › Text Direction, right-button menu: across, turned 90° (`tbRl`) or 270° (`btLr`), read and written; turned text is laid out along the cell (the row growing to fit it, or wrapping in an exact row), drawn turned on the screen (view.c `draw_turned`) and in PDFs (a turned text frame), with the caret across it and clicks finding it (core/layout.c `place_turned`, tests "turned text") [WP21b]
- ✅ Cell Margins (Alignment) `table.cellMargins` — `ID_TPROPS` (table cell margins), and WP21b's Cell Options (per-cell margins, `w:tcMar`, laid out per cell; tests "cell margins")
- ✅ Sort (Data) `table.sort` — `ID_TSORT`
- ✅ Repeat Header Rows (Data) `table.repeatHeader` — `ID_HEADERROW`
- ✅ Convert to Text (Data) `table.toText` — `ID_TOTEXT`
- ✅ Formula (Data) `table.formula` — `ID_FORMULA`

### Picture Format (14: ✅ 14 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Remove Background (Adjust) `picture.removeBackground` — `ID_PIC_REMOVEBG` (amiga/wp_picture.c): the corner colour and those near it made transparent (`ObjFx.bgkey`); saved drawn into the picture with `a14:backgroundRemoval`, Set Transparent Color as `a:clrChange` [WP17]
- ✅ Corrections (Adjust) `picture.corrections` — `ID_PIC_CORRDD` (sharpen/soften, brightness, contrast) and the Format Picture pane's sliders; `a:lum`, `a14:sharpenSoften` [WP17]
- ✅ Color (Adjust) `picture.color` — `ID_PIC_COLORDD`: saturation, temperature, greyscale, sepia, washout, black and white, recolour dark/light, Set Transparent Color; `a:grayscl`, `a:biLevel`, `a:duotone`, `a:clrChange`, `a14:saturation`, `a14:colorTemperature` [WP17]
- ✅ Artistic Effects (Adjust) `picture.effects` — `ID_PIC_ARTDD`: blur, pencil greyscale, line drawing, photocopy, cutout, film grain, glow edges, mosaic (core/objfx.c); saved drawn into the picture with the original as `a14:imgLayer`, as Word does [WP17]
- ✅ Transparency (Adjust) `picture.transparency` — `ID_PIC_ALPHADD`, `a:alphaModFix`; see-through on the screen (ordered pattern) and in PDFs (soft mask) [WP17]
- ✅ Compress Pictures (Adjust) `picture.compress` — `ID_PIC_COMPRESS`: 220/150/96 ppi, delete cropped areas, this picture or all; JPEG and PNG writers from Easel (core/picfmt.c) [WP17]
- ✅ Change Picture (Adjust) `picture.change` — `ID_PIC_CHANGE` (size and formatting kept) [WP17]
- ✅ Reset Picture (Adjust) `picture.reset` — `ID_PIC_RESETDD` (Reset Picture, Reset Picture & Size) [WP17]
- ✅ Picture Styles (Picture Styles) `picture.style` — `ID_PIC_STYLEDD`: 14 presets (frames, drop and centre shadows, rounded and snipped corners, ovals, soft edges, metal, rotated), and Picture Effects `ID_PIC_EFFECTSDD`; the picture's geometry (`a:prstGeom`) and `a:effectLst` read and written [WP17]
- ✅ Picture Border (Picture Styles) `picture.border` — `ID_OBJLINEDD`, `ID_OBJWEIGHTDD`, `ID_OBJDASHDD`
- ✅ Alt Text (Accessibility) `picture.altText` — `ID_ALTTEXT`
- ✅ Crop (Size) `picture.crop` — `ID_OBJCROP` (by numbers); WP17: crop by dragging the handles (`ID_PIC_CROP`, crop marks on the frame), Crop to Shape, Aspect Ratio
- ✅ Size (Size) `picture.size` — `ID_OBJSIZE`
- ✅ Rotation (Size) `arrange.rotation` — `ID_OBJROTDD`, round handle

### Shape Format (13: ✅ 13 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Alt Text (Accessibility) `object.altText` — `ID_ALTTEXT` (Shape Format's Accessibility group, `o_salt`)
- ✅ Shape Fill (Shape Styles) `shape.fill` — `ID_OBJFILLDD` (solid colours)
- ✅ Shape Fill Gradient (Shape Styles) `shape.fill` — `ID_SH_GRADDD` (light and dark variations, linear, radial, from the corners), the pane's gradient settings; picture fills `ID_SH_PICFILL`; `a:gradFill`, a shape's `a:blipFill` [WP18]
- ✅ Format Shape Pane (Shape Styles) `view.formatShapePane` — `ID_SH_PANE`: a pane window beside the page that follows the selection and changes the object as you set it (fill, line, shadow, glow, soft edges, size, rotation, text box alignment, direction and margins; for a picture its corrections, colour, transparency and artistic effect) [WP18]
- ✅ Shape Outline (Shape Styles) `shape.outline` — `ID_OBJLINEDD`
- ✅ Shape Effects (Shape Styles) `shape.effects` — `ID_SH_EFFECTSDD`: shadows, glows, soft edges (`a:outerShdw`, `a:glow`, `a:softEdge`), drawn on the screen and in PDFs [WP18]
- ✅ WordArt Quick Styles (WordArt Styles) `wordArt.style` — Shape Format › WordArt Styles › Quick Styles (and Format › WordArt Styles...): a style on the selected shape's text, or the selected text [WP19]
- ✅ Text Fill (WordArt Styles) `wordArt.textFill` — no fill, eight colours, five gradients (linear and from the centre): `w14:textFill` [WP19]
- ✅ Text Outline (WordArt Styles) `wordArt.textOutline` — no outline, eight colours, three weights: `w14:textOutline`; the Text Effects list beside it: shadows (`w14:shadow`), reflections (`w14:reflection`), glows (`w14:glow`) and bevels (`w14:props3d` bevelT with `w14:scene3d`, drawn as a light and dark edge on screen; flat in PDFs) [WP19]
- ✅ Text Effects Transform (WordArt Styles) `wordArt.transform` — 12 warps drawn bent by mapping the glyph outlines (arch up/down, circle, wave, slant up/down, inflate, deflate, chevron and triangle up/down), on screen (turned with the box) and in PDFs; `a:prstTxWarp` read and written in `wps:bodyPr`; every other DrawingML preset kept by name and drawn flat (`Image.warp`) [WP19]
- ✅ Text Direction (Text) `shape.textDirection` — `ID_SH_TEXTDIRDD` (`bodyPr vert`: across, turned 90° or 270°, drawn turned on the screen and in PDFs) [WP18]
- ✅ Align Text (Text) `shape.alignText` — `ID_SH_ALIGNDD` (`bodyPr anchor`: top, middle, bottom; core/objtext.c) [WP18]
- ✅ Create Link (Text) `shape.link` — `ID_SH_LINK` (then click the empty box) and Break Link `ID_SH_UNLINK`; the chain's text flows from box to box (core/objtext.c), `wps:txbx id`/`wps:linkedTxbx` read and written [WP18]

### Header & Footer (5: ✅ 5 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Go to Header (Navigation) `insert.editHeader` — `ID_HFHEADER`
- ✅ Go to Footer (Navigation) `insert.editFooter` — `ID_HFFOOTER`
- ✅ Previous Section (Navigation) `hf.previous` — `ID_HFPREV` (`headfoot_section(-1)`)
- ✅ Next Section (Navigation) `hf.next` — `ID_HFNEXT`
- ✅ Header from Top (Position) `hf.position` — `ID_PAGESETUPDLG` on the Header & Footer tab (header and footer distances)

### Equation (22: ✅ 21 · 🟨 0 · ⬜ 0 · ➖ 1)

- ✅ Equation (Tools) `insert.equation` — equations read, laid out, drawn and written (see 2.2); the Equation button (Insert › Symbols, the Equation tab's Tools, Insert › Equation Tools › Insert New Equation, Alt+=) puts a new equation in place and the caret in it, the selected text built up into it (WP14b); Insert › Equation... (amiga/wp_math.c) types one in linear format: the equation typed in linear format (UnicodeMath subset: a/b, x^2, x_i, √(…), ∑_(i=1)^n▒…, brackets, functions, accents, ■ matrices, █ arrays, □ ▭ ⟡ boxes, "text"; characters outside Latin-1 as \control words), a display equation when the paragraph is empty as in Word; with an equation selected (or "Edit Equation..." on the right-button menu) it is changed. Checked: tests/test_wp14a.c (linear format in and out), emulator screenshot of the dialog [WP14a]
- ➖ Ink Equation (Tools) `insert.inkEquation` — WordCraft doesn't have it either
- ✅ Unicode (Conversions) `equation.inputFormat` — Equation › Unicode (and Format › Equation): typing builds up UnicodeMath as Word does (autobuild-up on a space or an operator: x^2, a/b, √(…), \alpha …; brackets still open wait), core/mathedit.c `me_build_up`, `me_autocorrect`; checked: tests/test_wp14b.c, emulator (x^2 typed in an equation became a superscript) [WP14b]
- ✅ LaTeX (Conversions) `equation.inputFormat` — Equation › LaTeX: typed LaTeX builds up on a space (\frac, \sqrt[n]{}, ^{} _{}, \sum/\int with limits, \left…\right, matrix/pmatrix/bmatrix/vmatrix/cases/aligned, accents, \overline, \overbrace, \text, \mathbb, Greek and 200-odd symbols), core/mathtex.c `math_from_latex`/`math_to_latex`; checked: tests/test_wp14b.c (LaTeX in, out and back) [WP14b]
- ✅ Convert (Conversions) `equation.convert` — Current/All - Professional/Linear (the list, Format › Equation, the right-button menu, Ctrl+= in an equation): the Linear form is the equation's UnicodeMath or LaTeX text in one run, built up again by Professional (`me_to_linear_form`, `me_to_professional`); checked: tests/test_wp14b.c, emulator (Ctrl+= there and back) [WP14b]
- ✅ Normal Text (Conversions) `equation.normalText` — typing goes into normal-text runs (`m:nor`), and the selection's runs are switched (`me_set_normal`); checked: tests/test_wp14b.c [WP14b]
- ✅ Symbols (Symbols) `equation.insertSymbol` — a popup with WordCraft's eight sets (Basic Math, Greek Letters, Letter-Like Symbols, Operators, Arrows, Negated Relations, Scripts, Geometry; core/mathgal.c), each symbol drawn by the equation layout, its code point shown under the pointer; outside an equation it starts one; checked: emulator screenshot [WP14b]
- ✅ Fraction (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Script (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Radical (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Integral (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Large Operator (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Bracket (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Function (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Accent (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Limit and Log (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Operator (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Matrix (Structures) `equation.insertStructure` — its gallery (WordCraft's templates, core/mathgal.c) in a popup of drawn previews with dotted placeholders; the template goes in at the caret, a selection into its main argument, the caret to its first empty argument; outside an equation a new one holds it; also Insert › Equation Tools; checked: tests/test_wp14b.c (every template builds), emulator screenshots (Fraction and Matrix galleries) [WP14b]
- ✅ Change to Inline / Display (Equation Options) `equation.display` — Display moves the equation onto a paragraph of its own; also on the right-button menu and Format › Equation; checked: emulator [WP14b]
- ✅ Justification (Equation Options) `equation.justify` — Left, Right, Centered, Centered as Group (`m:oMathParaPr/m:jc`, layout's `math_line_shift`); list, right-button submenu, Format › Equation [WP14b]
- ✅ Equation Number (#) (Equation Options) `equation.number` — kept as Word does: the equation an `m:eqArr` of one row ending `#(n)` (typing `expr#(n)` numbers it too); laid out with the number at the line's right edge and the equation justified in the rest (core/mathlay.c, `math_fit` from core/layout.c); automatic numbers (1), (2)… renumbered in document order like SEQ fields (`math_renumber`); checked: tests/test_wp14b.c (layout spans the line, .docx round trip, renumbering), emulator screenshot (numbers at the right margin) [WP14b]
- ✅ Structure Commands (Equation Options) `equation.structure` — the commands that apply at the caret (Word's right-click actions: insert/delete matrix rows and columns, equation-array rows, bracket arguments, change brackets ( ) [ ] { } ⟨ ⟩ | | ‖ ‖ ⌊ ⌋ ⌈ ⌉ or none, stretch, fraction type, show/hide degree and limits, limit location, add/remove scripts, flip group characters and bars, remove the structure) in a popup, Format › Equation and the right-button submenu (`me_actions`, `me_apply`); checked: tests/test_wp14b.c, emulator screenshot [WP14b]

### Outlining (13: ✅ 13 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Promote to Heading 1 (Outline Tools) `outline.promoteToHeading1` — `ID_OL_TOH1` (`outline_set_level()`) [WP11]
- ✅ Promote (Outline Tools) `outline.promote` — `ID_OL_PROMOTE` (a collapsed heading's part with it; body text takes the heading above's level) [WP11]
- ✅ Outline Level (Outline Tools) `outline.level` — `ID_OL_LEVEL` field (Level 1-9, Body Text) [WP11]
- ✅ Demote (Outline Tools) `outline.demote` — `ID_OL_DEMOTE` (Heading 4-9 made like the one above when missing: `outline_heading_style()`) [WP11]
- ✅ Demote to Body Text (Outline Tools) `outline.demoteToBody` — `ID_OL_TOBODY` (the Normal style) [WP11]
- ✅ Move Up (Outline Tools) `outline.moveUp` — `ID_OL_UP` (past the part shown above; folds go with the paragraphs) [WP11]
- ✅ Move Down (Outline Tools) `outline.moveDown` — `ID_OL_DOWN` [WP11]
- ✅ Expand (Outline Tools) `outline.expand` — `ID_OL_EXPAND`, Alt-Shift-+, double-click a plus (a level at a time, as Word) [WP11]
- ✅ Collapse (Outline Tools) `outline.collapse` — `ID_OL_COLLAPSE`, Alt-Shift-- (deepest open headings first; a dotted line under a collapsed heading) [WP11]
- ✅ Show Level (Outline Tools) `outline.showLevel` — `ID_OL_SHOWLEVEL` field, Alt-Shift-1..9 and A [WP11]
- ✅ Show Text Formatting (Outline Tools) `outline.showFormatting` — `ID_OL_FORMAT` (off: all text in the Normal style's look, `OL_PLAIN`) [WP11]
- ✅ Show First Line Only (Outline Tools) `outline.firstLineOnly` — `ID_OL_FIRSTLINE` (`OL_FIRST`, "..." after a cut paragraph) [WP11]
- ✅ Close Outline View (Close) `outline.close` — `ID_OL_CLOSE` (back to Print Layout) [WP11]

### SmartArt Design (7: ✅ 7 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Add Shape (Create Graphic) `smartArt.addShape` [WP15c] — split button: after (the button), before, above, below the item chosen in the Text Pane (`sa_add`); also Format > SmartArt. A SmartArt from Word is read back as an outline (`sa_from_pack`: its data part's points and parent connections) and becomes editable; a change writes all its parts afresh from the model (Word's own per-shape changes are then lost)
- ✅ Promote (Create Graphic) `smartArt.promote` [WP15c] — with the items under it; also Move Up, Move Down (past the neighbour with its items) and Right to Left (`dgm:dir rev`, the drawing mirrored)
- ✅ Demote (Create Graphic) `smartArt.demote` [WP15c] — not past the item before it, as Word
- ✅ Text Pane (Create Graphic) `smartArt.textPane` [WP15c] — a window with the outline (indented), the chosen item's text (Return puts it in) and Add/Before/Above/Below/Delete/Promote/Demote/Move Up/Move Down; follows the selected graphic
- ✅ Layouts (Layouts) `smartArt.layout` [WP15c] — split: the button opens the layout chooser with previews, the arrow lists the 21 layouts: Basic Block List, Vertical and Horizontal Bullet List, Basic/Vertical/Basic Chevron Process, Basic and Text Cycle, Basic Radial, Organization Chart, Hierarchy, Horizontal Hierarchy, Basic/Linear Venn, Basic Target, Basic/Titled/Grid Matrix, Basic/Inverted Pyramid, Pyramid List (layout definitions of Quill's own under Office's layout ids; ported in part from WordCraft's smart_art.rs)
- ✅ Change Colors (SmartArt Styles) `smartArt.colors` [WP15c] — Accent 1-6 (Office's accentN_2) and Colorful; with them the SmartArt Styles list: Simple Fill, White Outline, Subtle/Moderate/Intense Effect (outlines, and shadows Word and LibreOffice show; Quill draws no shadows)
- ✅ Reset Graphic (Reset) `smartArt.reset` [WP15c] — laid out afresh from its text in Simple Fill, left to right (a graphic from Word loses its own shape changes)

### Chart Design (5: ✅ 5 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Change Chart Type (Type) `chart.type` [WP15b] — the Insert Chart dialog with the chart's own data previewed in each kind (data reshaped: pie takes the first series, scatter X values from the categories, bubble sizes). The Chart Design tab (amiga/wp_chart.c) also has Switch Row/Column, Select Data (series names, order, remove), Quick Layout (10), Change Colors (colourful, monochrome, accents), Chart Styles (8) and Format (each part's fill and outline, titles, value axis minimum/maximum/unit and number format, legend, labels, gridlines); also Format > Chart. A chart from Word is edited through WP15a's model and its parts written afresh (its chart style and colour parts, theme and font kept; its own text formatting lost)
- ✅ Edit Data (Data) `chart.editData` [WP15b] — a sheet of the chart's data (series names in row 1, categories in column A; 9 x 6 cells shown, scrolled, rows and columns set by sliders), written back to the chart and its embedded workbook
- ✅ Chart Title (Chart Layouts) `chart.title` [WP15b] — Add Chart Element: chart title none/above, axis titles; the text in Format
- ✅ Legend (Chart Layouts) `chart.legend` [WP15b] — Add Chart Element: none, right, top, left, bottom; gridlines across and up
- ✅ Data Labels (Chart Layouts) `chart.dataLabels` [WP15b] — Add Chart Element: none, show (the values)

### Editing (41: ✅ 41 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Type Text (Keyboard) `text.insert` — view.c `key()`, `CMD_TEXT`
- ✅ New Paragraph (Keyboard) `text.newParagraph` — Return
- ✅ Line Break (Keyboard) `text.lineBreak` — Shift-Return, `ID_LINEBREAK`
- ✅ Column Break (Keyboard) `text.columnBreak` — `ID_COLBREAK` (Breaks list; no Ctrl+Shift+Return)
- ✅ Tab (Keyboard) `text.tab` — Tab (list levels, table cells)
- ✅ Tab Character (Keyboard) `text.insertTab` — Alt-Tab in a table
- ✅ Backspace (Keyboard) `text.backspace`
- ✅ Delete (Keyboard) `text.delete`
- ✅ Delete Previous Word (Keyboard) `text.deleteWordBack` — Alt-Backspace
- ✅ Delete Next Word (Keyboard) `text.deleteWordForward` — Alt-Del
- ✅ Move Paragraph Up (Keyboard) `para.moveUp` — Ctrl-Alt-Shift-Up anywhere (Alt-Shift-Up selects by paragraph in Quill), Alt-Shift-Up too in Outline view: `outline_move()` (a collapsed heading with its part) [WP11]
- ✅ Move Paragraph Down (Keyboard) `para.moveDown` — Ctrl-Alt-Shift-Down; Alt-Shift-Down in Outline view [WP11]
- ✅ Promote (Keyboard) `para.promote` — Ctrl-Alt-Shift-Left (headings by their Heading styles, `outline_promote()`); in Outline view also Alt-Shift-Left and Shift-Tab; Shift-Tab raises a list item's level elsewhere [WP11]
- ✅ Demote (Keyboard) `para.demote` — Ctrl-Alt-Shift-Right; in Outline view also Alt-Shift-Right and Tab [WP11]
- ✅ Toggle Unicode Character (Keyboard) `text.toggleUnicode` — Ctrl-X (Alt-X types a character on Amiga keymaps), `fx_toggle_unicode()`: hex code (up to six digits, U+ allowed) to character and back (00E9, U+ after hex digits) [WP12]
- ✅ Spike (Keyboard) `edit.spike` — Ctrl-F3 (`ID_FX_SPIKE`): the selection cut and added, with its formatting, to the Spike (a document of its own; the clipboard is untouched) [WP12]
- ✅ Insert Spike (Keyboard) `edit.insertSpike` — Ctrl-Shift-F3 (`ID_FX_INSSPIKE`): the Spike pasted with its formatting and emptied [WP12]
- ✅ Word Left (Navigation) `caret.wordLeft` — Alt-Left
- ✅ Word Right (Navigation) `caret.wordRight` — Alt-Right
- ✅ Line Start (Navigation) `caret.home` — Home, Ctrl-Left
- ✅ Line End (Navigation) `caret.end` — End, Ctrl-Right
- ✅ Paragraph Up (Navigation) `caret.paraUp` — Alt-Up
- ✅ Paragraph Down (Navigation) `caret.paraDown` — Alt-Down
- ✅ Document Start (Navigation) `caret.docStart` — Ctrl-Home, Ctrl-Alt-Up
- ✅ Document End (Navigation) `caret.docEnd` — Ctrl-End, Ctrl-Alt-Down
- ✅ Page Up (Navigation) `caret.pageUp` — Page Up
- ✅ Page Down (Navigation) `caret.pageDown` — Page Down
- ✅ Go Back (Navigation) `edit.goBack` — Shift-F5 or Ctrl-Alt-Z (`ID_FX_GOBACK`): the last four places edited (from the undo history) in turn [WP12]
- ✅ Select Word (Selection) `select.word` — double-click (view.c)
- ✅ Select Sentence (Selection) `select.sentence` — `ID_SELECTDD` → `CMD_SELUNIT` 0
- ✅ Select Paragraph (Selection) `select.paragraph` — `CMD_SELUNIT` 1, triple-click
- ✅ Extend Selection (F8) (Selection) `select.extend` — F8 on, again a word, sentence, paragraph, section, the document; Shift-F8 shrinks; the cursor keys and clicks extend, a typed character extends to the next like it; Esc ends it (status bar says so) [WP12]
- ✅ Column Selection (Selection) `select.column` — Ctrl-Shift-F8 or Alt-click/drag (`fx_block()`): a block of the lines between the corners, drawn as such; Copy/Cut/Delete/Backspace and character and paragraph formatting act on each line's part [WP12]
- ✅ Show Field Codes (Fields) `view.fieldCodes` — WP1: `ID_FLD_SHOWCODES` (Alt-F9, References › Fields › Show Codes, View › Field Codes): every field shows { code } in place of its result (layout.c `make_boxes`; a result over several paragraphs hides only its first paragraph's part); the PDF still shows the results [WP1]
- ✅ Toggle Field Codes (Fields) `fields.toggleCode` — WP1: `ID_FLD_TOGGLE` (Shift-F9, field menu): the selected fields, or the one at the caret, the other way round [WP1]
- ✅ Edit Field Code (Fields) `fields.setCode` — WP1: `ID_FLD_EDIT` (field menu): the Field window with the field's code, `field_set_code()` then an update; undoable [WP1]
- ✅ Unlink Fields (Fields) `fields.unlink` — WP1: `ID_FLD_UNLINK` (Ctrl-Shift-F9, Ctrl-6): results become plain text, page fields their page's number [WP1]
- ✅ Lock Fields (Fields) `fields.lock` — WP1: `ID_FLD_LOCK` (Ctrl-3; Ctrl-F11 where the keyboard has F11), saved as `w:fldLock` [WP1]
- ✅ Unlock Fields (Fields) `fields.unlock` — WP1: `ID_FLD_UNLOCK` (Ctrl-4; Ctrl-Shift-F11) [WP1]
- ✅ Next Field (Fields) `fields.next` — WP1: `ID_FLD_NEXT` (References › Fields, Insert › Fields submenu; F11 where the keyboard has one: Amiga keyboards stop at F10) selects it [WP1]
- ✅ Previous Field (Fields) `fields.previous` — WP1: `ID_FLD_PREV` (Shift-F11 likewise) [WP1]

### Tools (7: ✅ 4 · 🟨 0 · ⬜ 3 · ➖ 0)

- ✅ AutoCorrect Options (Proofing) `tools.autocorrect` — Review > Proofing > AutoCorrect Options: the AutoCorrect and AutoFormat As You Type pages, the replacement list, Exceptions (core/autocorrect.c, PROGDIR:Quill.autocorrect) [WP8]
- ✅ Set Proofing Language (Proofing) `review.language` — the Language window (mark the selection, do not check, Set As Default), also from the status bar's language [WP31]
- ⬜ Customize Ribbon (Customize) `tools.customizeRibbon` [WP26]
- ⬜ Customize Keyboard (Customize) `tools.customizeKeyboard` [WP26]
- ⬜ Encrypt with Password (Protection) `file.encrypt` [WP7]
- ✅ Templates and Add-ins (Templates) `tools.templates` — Developer › Templates › Document Template and File › Templates and Add-ins… (`ID_TP_TEMPLATES`): the attached template (`w:attachedTemplate` with its settings relationship, app.xml Template), Attach…, Automatically update document styles (`w:linkStyles`: the template's styles copied in, also on opening), the personal templates listed, Organizer; global add-ins ➖ (no macros) [WP23]
- ✅ Full Screen View (Views) `view.fullScreenView` — `ID_FOCUS` hides the ribbon, rulers, pane and status bar (`chrome()`)

## 2. Beyond the ribbon

Same marks as above. WordCraft's side comes from its docs (file-format-parity.md, layout-parity.md, typing-parity.md, localization-parity.md, target-app-parity.md) and its source (crates/docx, crates/formats, crates/docbin, crates/layout, crates/pdf, crates/proof, crates/zotero, crates/mcp, crates/ui-egui/src/options.rs, crates/engine/src/keymap.rs). Quill's side was checked in core/docx.c, core/layout.c, core/pdf.c, core/doc.h, amiga/main.c, amiga/view.c, amiga/panes.c and amiga/backstage.c.

### 2.1 File formats

- ✅ **.docx read and write** — core/docx.c (`docx_read`/`docx_write`), the only format Quill saves with formatting.
- 🟨 **.docx Strict Open XML (read)** — untested in Quill: it finds the main part by the relationship's name ending (`rel_target(..., "/officeDocument")`, docx.c) and matches elements by local name, so a Strict file may open, but Strict's unit-suffixed measures (`13.90pt`, `50%`) aren't parsed. WordCraft maps the Strict namespaces and units (`xml.rs`, #2329).
- 🟨 **.dotx / .docm / .dotm** — (WP23) .dotx and .dotm are read as templates (the main part's content type) and open as a new untitled document attached to them; Save as Template writes .dotx or .dotm (template content types; `fmt_dotx`/`fmt_dotm` rows). Macros are dropped (➖: no macros), as before.
- ⬜ **.doc Word 97-2003 (read)** — WordCraft has a spec-based reader (crates/docbin: text, formatting, styles, sections, tables, lists, notes, fields, pictures, shapes, RC4/XOR encryption). Quill: none.
- ✅ **.rtf read and write** [WP28a] — core/rtf.c (row `fmt_rtf`, sniffed by `{\rtf`; Open/Save As take .rtf, File > Save As Rich Text... saves a copy): text, character and paragraph formatting, styles (\sbasedon, \snext; a file's unused styles aren't made), lists (\listtable/\ls and Word 6's \pn), tables with merged cells (by width, \clmgf/\clmrg, \clvmgf/\clvmrg), borders, shading, row heights, heading rows; PNG/JPEG pictures (hex or \bin, \shppict, shapes' pib), WMF/EMF/bitmaps as a dashed box of their size; headers/footers (default, first, even), footnotes, endnotes, HYPERLINK/PAGE/NUMPAGES/PAGEREF fields (others keep their result), bookmarks, tracked changes (\revtbl, \revised, \deleted), sections with page setup, columns, line numbers, vertical alignment, \info title/author; code pages 437/850/852/866/874/1250-1258/10000 (core/codepage.c), \uN with \uc fallbacks and surrogates. Evidence: tests/test_wp28a.c (a document with all of these round-trips with same_doc and writes the same file twice; Word's and LibreOffice's 7 test .docx files through RTF; RTF that LibreOffice wrote (tests/data/lo-*.rtf) reads as the .docx does); tools/lo_formats.sh: LibreOffice converts what Quill writes to .docx with every paragraph's text, notes, tables and pictures, and LibreOffice renders it as it renders the original .docx (checked by eye with pdftoppm). 🟨 left out: nested tables are flattened into their cell (read and write, LOST_TABLES), shapes/text boxes and comments aren't written, floating pictures are written in line, picture cropping, double-byte code pages without \u (932/936/949/950 text shows as '?').
- ✅ **.odt read and write** [WP28b] — core/odt.c reads, core/odtw.c writes (rows `fmt_odt` for .odt/.ott, sniffed by the package's mimetype, and `fmt_fodt` for Flat ODT .fodt; File > Save As OpenDocument Text... saves a copy): content.xml, styles.xml, meta.xml, META-INF/manifest.xml (mimetype stored first); paragraph and character styles, common and automatic, with parents, next styles and the default style (only used styles are made), fonts (font-face declarations), paragraph properties (alignment, indents, spacing, line spacing, tabs with leaders, keeping, widows, breaks, borders, padding, shading, contextual spacing, drop caps) and text properties (font, absolute and relative sizes, bold, italic, underline kinds and colour, strike, double strike, super/subscript, raised text, caps, small caps, colour, highlight, letter spacing, scale, shadow, outline, relief, hidden, box); headings (text:h); lists (text:list nested by level, bullet and number list styles with prefixes, suffixes, levels shown and LibreOffice's num-list-format, start values, continued and restarted lists, continuation paragraphs in an item); tables (column widths absolute or relative, spans across columns and rows with covered cells, borders, padding, shading, vertical alignment, row heights, rows kept together, heading rows, alignment and indent, one table inside a cell; deeper ones flattened, LOST_TABLES); pictures in Pictures/ (base64 in Flat ODT) anchored as character, paragraph/character or page, with wrap (none, parallel, contour, run-through behind/in front), position, distances, cropping (fo:clip), mirroring, rotation, border and alt text; text boxes (draw:text-box) and shapes (custom shapes, rectangles, ellipses, lines; fill, line, text; groups); headers and footers from master pages (default, left = even, first, and a First Page master followed by another); footnotes and endnotes with their configuration; bookmarks; links; fields (page number, page count and a bookmark's page as Quill's placeholders; date, time, author, title, subject, keywords, description, file name, dates, word and character counts, sequence, user-defined properties as WP1's fields with their codes, others round-tripped through Quill's own marks); tracked insertions and deletions (text:tracked-changes; deleted paragraphs split as they show); comments (office:annotation over a range with office:annotation-end, resolved); sections (text:section with columns; a new page and its page setup and headers by master page); page size, margins, orientation, page border and colour, page number format and start, line numbering; equations as MathML formula objects (core/mathml.c: fractions, scripts, roots, n-ary operators, fences, functions, accents, bars, group characters, limits, matrices, equation arrays, boxes, phantoms, prescripts; inline or display by context); title, author and the other properties of meta.xml. Evidence: tests/test_wp28b.c, 77 checks (a document with all of these round-trips with same_doc and writes the same content.xml and styles.xml twice, Flat ODT the same file twice; the 10 test .docx files through ODT with same_doc, equations unchanged; LibreOffice-written .odt of word, lo, tables, headers, notes, pictures, equations and a .fodt read as their .docx; hand-written ODT for white space, lists, tables, changes, styles, master pages, MathML; damaged files). LibreOffice opens what Quill writes: every test document converted ODT → .docx keeps all its paragraphs (fmtconv -cmp, 10 of 10), and its PDFs look as LibreOffice's own of the .docx (checked by eye with pdftoppm). 🟨 left out: charts and SmartArt are written as a dashed box; section starts on even/odd pages become a new page; a continuous section whose headers differ starts a page (ODF changes headers only with a page); formatting revisions (text:format-change) are not read or written; Quill's own table styles are written as their look (cells keep it; the style itself only when it is one of Quill's gallery); note options of single sections become the document's.
- 🟨 **.txt** — Quill opens UTF-8 or Latin-1 and saves Latin-1 only (`ID_SAVETXT`, `save_as(1)`): characters outside Latin-1 are lost. WordCraft detects encodings and writes UTF-8.
- ✅ **.html / .htm (read and write)** [WP28c] — core/html.c (row `fmt_html`, by name; File > Save As Web Page... saves a copy): UTF-8 HTML5, paragraph styles as CSS classes (with their names, read back as styles), headings h1-h6, paragraph CSS (alignment, indents, spacing, line spacing, shading, borders, page break), b/i/u/s/sup/sub and spans (font, size, colour, highlight, small caps, caps, spacing), lists nested by level (ul/ol with type and start), tables with colspan/rowspan, column widths, padding, shading and borders (a table style's look is shown and marked data-look="style"), links, bookmarks, pictures as data: URIs (read from data: URIs or files next to the page, `doc_load_path`), footnotes/endnotes in LibreOffice's sdfootnote markup. Reads other programs' HTML too: font tags, center, blockquote, pre, hr, dl, entities, Windows 1252 bytes, `<style>` rules for p/h1-h6/.class. Evidence: tests/test_wp28c.c (round trip with same_doc; Word's and LibreOffice's .docx through HTML; LibreOffice's HTML of them, tests/data/lo-*.html with its picture files); tools/lo_formats.sh (LibreOffice reads what Quill writes: every paragraph, the notes and tables). Left out: headers, footers, sections and page setup (no place in HTML); nothing is fetched from the web.
- ➖ **.mht / .mhtml** — WordCraft doesn't have it either.
- ⬜ **Word XML: Flat OPC .xml and Word 2003 XML** — WordCraft reads and writes both through its DOCX code.
- ✅ **Markdown .md (read and write)** [WP28c] — core/markdown.c (row `fmt_markdown`, by name; File > Save As Markdown... saves a copy): CommonMark blocks (ATX/setext headings, paragraphs, block quotes as Quote, nested bullet/ordered lists with their start numbers, fenced and indented code as Code-style paragraphs, thematic breaks, link reference definitions) and inlines (emphasis by CommonMark's delimiter rules, code spans, links inline/reference/autolinks, images from data: URIs or files, hard breaks, escapes, entities, inline b/i/u/s/sup/sub/code/br), GitHub tables with alignment, strikethrough and footnotes. Writes the same (underline, super/subscript as HTML tags; pictures as data: URIs). Evidence: tests/test_wp28c.c (round trip of text, headings, emphasis, lists, code, tables, pictures, notes; writing twice gives the same file; CommonMark cases); tools/lo_formats.sh: pandoc (-f gfm) reads every paragraph Quill writes. Left out: fonts, sizes, colours, alignment, page layout (Markdown has none).
- ⬜ **LaTeX .tex (read and write, with math)** — WordCraft crates/formats/src/latex.
- 🟨 **PDF export** — Quill core/pdf.c: pages as laid out, embedded TrueType/OpenType fonts, URI link annotations. WP29 added: font subsetting (core/pdffont.c: TrueType glyf/loca rebuilt, CFF CharStrings cut in place; the short test documents' PDFs shrink 10-16 times, e.g. tables.docx 415 KB → 30 KB, word.docx 769 KB → 49 KB, wordcraft-long.docx 1.9 MB → 0.88 MB), any character the font has through Type 0 fonts with Identity-H and ToUnicode CMaps (pdffonts: "CID TrueType / CID Type 0C (OT), emb sub uni"; pdftotext extracts Greek and Cyrillic), internal links (`#bookmark` links, PAGEREF cross-references, Quill's own TOC entries to their headings, footnote/endnote references and their numbers back), the outline from the headings (nested, /PageMode /UseOutlines), bookmarks as named destinations, Title/Author/Subject/Keywords (UTF-16 when needed), a page range (Export Options); qpdf --check clean [WP29]. Still missing: tagged PDF (structure tree; not part of WP29).
- ➖ **PDF import** — WordCraft doesn't have it either.
- ✅ **Export page as image (.png)** — WP29: Export Image writes PNG or IFF ILBM pages (core/pngwrite.c, core/raster.c), with Export Options for the pages and resolution [WP29]
- ⬜ **Encrypted .docx (open and save with a password)** — WordCraft crates/docx/src/crypt (ECMA-376 agile AES-256, standard encryption). Quill none.
- 🟨 **Spreadsheets (.xlsx, .xls, .ods, .csv) as mail-merge recipient lists** — WordCraft crates/formats/src/sheet. WP4: .xlsx (first sheet), CSV/tab-separated text and a .docx's table are read (core/mergedata.c); .xls and .ods aren't (Quill says to save as .xlsx or CSV) [WP4]
- 🟨 **Picture formats for Insert › Picture** — Quill: PNG and JPEG (baseline, progressive, CMYK; core/image.c); WP17: GIF, BMP and IFF ILBM (EHB, HAM6/8, deep, PBM; readers from Easel in core/picfmt.c), GIF and BMP saved as they are, ILBM as PNG. Not yet: WebP, WMF and EMF (WordCraft has them).
- ➖ **.json document model** — WordCraft's format for agents (control protocol, left out on purpose).

### 2.2 DOCX features

- 🟨 **Run and paragraph properties, styles, numbering, sections** — Quill reads and writes the common set (core/docx.c). Missing: complex-script properties (`w:rtl`, `w:cs`, `w:szCs`, `w:bCs`) and East Asian font slots (`w:eastAsia`), every `ST_NumberFormat` beyond decimal/letters/roman (`NF_` in doc.h), indents in characters, more than 8 tab stops a paragraph (`ParaFmt.tabs[8]`), hidden, emboss, imprint, outline, shadow, underline colour, `w:outlineLvl`.
- 🟨 **Positional tabs (`w:ptab`)** — read as an ordinary tab (docx.c line 1355), not placed by alignment/margin.
- 🟨 **Tables** — widths, merges, borders, shading, styles with conditional formatting, nested tables (one level), cantSplit, heading rows: ✅. Floating tables (`w:tblpPr`) ✅ (WP21b: placed where they say, the text wrapping round them, written back), cells' text direction (`w:textDirection`), own margins (`w:tcMar`) and tables' alt text (`w:tblCaption`, `w:tblDescription`) ✅ (WP21b), custom table styles ✅ (WP21a); row exceptions (`w:tblPrEx`) and right-to-left tables (`w:bidiVisual`) not read; tables in headers and footers come in as their text (README roadmap).
- ✅ **Headers/footers, page borders, line numbers, gutter, mirror margins** — first/even/odd, per section, link to previous: ✅. WP22a: `w:pgMar w:gutter`, `w:rtlGutter`, `w:gutterAtTop`, `w:mirrorMargins`, `w:printTwoOnOne`, `w:bookFoldPrinting`/`w:bookFoldRevPrinting`/`w:bookFoldPrintingSheets`, `w:bordersDoNotSurroundHeader/Footer`; page borders per section and per side (`w:pgBorders` every attribute), read and written (tests/test_wp22a.c).
- ✅ **Hyphenation settings** — `w:autoHyphenation`, `w:hyphenationZone`, `w:doNotHyphenateCaps`, `w:consecutiveHyphenLimit` read, written and laid out (WP22a: core/pages.c `pages_hyph_zone`, `pages_hyph_allowed`).
- ✅ **Footnotes, endnotes, comments** — read and written. Note options (`w:footnotePr`/`w:endnotePr`) and the document's own separators and continuation notice read and written (WP22b ✅). Comments' replies, resolved state, durable ids and UTC times, and the people (`commentsExtended`, `commentsIds`, `commentsExtensible`, `people.xml`, each person's own entry kept) ✅ (WP5, core/docx.c `wp5_read_parts()`, `wp5_write_parts()`).
- ✅ **Tracked insertions and deletions** — `w:ins`/`w:del` with author and date (docx.c, `REV_INS`/`REV_DEL`).
- 🟨 **Formatting revisions (`w:rPrChange`, `w:pPrChange`, `w:sectPrChange`, table ones, `w:numberingChange`)** — WP5: a run's and a paragraph's (`CharFmt.frev`, `ParaFmt.prev`, core/review.c) read with the old formatting (and its XML kept verbatim), shown (balloons, bars, Original), accepted and rejected, recorded while tracking (every formatting command) and written; `w:tblPrChange`, `w:tblGridChange`, `w:trPrChange`, `w:tcPrChange`, inserted and deleted rows and cells, `w:sectPrChange` kept verbatim with their table, row, cell or section (accepted by dropping them; rejecting them is refused). Missing: the legacy `w:numberingChange` and revisions inside style definitions (styles.xml) are dropped; a paragraph mark's own insertion, deletion and formatting change are not kept.
- ✅ **Move tracking (`w:moveFrom`/`w:moveTo`)** — WP5: read as a deletion and an insertion that are one move (`CharFmt.move`, by the name of its `w:moveFromRangeStart`/`w:moveToRangeStart`), drawn in the move colour and marks, written back as `w:moveFrom`/`w:moveTo` inside named ranges (LibreOffice reads them as a move). Quill's own cut and paste while tracking is recorded as a deletion and an insertion, not a move.
- ✅ **Fields** — WP1 (core/field.h): every field is kept with its code and cached result (marks round the result in the text, `Doc.fields`), from `w:fldChar`/`w:instrText` and `w:fldSimple`, nested (in results and in codes), with `w:fldLock`, `w:dirty`, no separator (XE, TC) and `w:ffData` verbatim, in the body, headers, footers, notes, comments and text boxes, and written back as complex fields: DATE, SEQ, MERGEFIELD, ADDIN (Zotero ZOTERO_ITEM/ZOTERO_BIBL, Mendeley, EndNote), IF, DOCPROPERTY, INDEX, XE, TC, REF, PAGEREF, HYPERLINK... survive a save; most are evaluated (see Update Fields); the TOC keeps its code; docProps core.xml, app.xml and custom.xml are read for the properties fields show, and core.xml's are written back. Checked: tests/test_wp1.c (82 checks), a document with 18 kinds of field round-tripped twice byte-stable and opened in LibreOffice with the same results.
- ⬜ **Content controls (`w:sdt`)** — Quill reads their content and drops the controls (docx.c treats `sdt`/`sdtContent` as transparent); WordCraft keeps and edits every kind (rich/plain text, checkbox, combo, drop-down, date, picture, building block, repeating section, data binding kept verbatim).
- ⬜ **Restrict Editing (`w:documentProtection`, `w:permStart`/`w:permEnd`, locked styles)** — not read or kept.
- 🟨 **Legacy form fields (`w:ffData`: FORMTEXT, FORMCHECKBOX, FORMDROPDOWN)** — WP1 keeps them as fields with their `w:ffData` verbatim and their results, through a save; they are not drawn as boxes or filled in (WP16).
- 🟨 **DrawingML pictures, inline and anchored, wrapping** — ✅ for square, tight (with `wp:wrapPolygon`), top and bottom, behind, in front; `wrapThrough` is read as tight (docx.c line 1264); `wrapText` sides (left/right/largest) not kept.
- ⬜ **WordArt text effects (`w14:textFill`, `w14:textOutline`, `w14:shadow`, `w14:glow`, `w14:reflection`)** — not read.
- ⬜ **Legacy font effects (`w:outline`, `w:shadow`, `w:emboss`, `w:imprint`)** — not read.
- ⬜ **OpenType ligatures (`w14:ligatures`)** — not read.
- 🟨 **DrawingML shapes and text boxes (`wps:`)** — 24 presets (`SH_` in doc.h), solid fill and outline, theme colours with tints and shades, text box text; WP18: 69 more presets (basic, block arrows, flowchart, callouts, stars and explosions; core/shapex.c), gradient and picture fills, shadow/glow/soft edge effects, `bodyPr` vertical text, anchoring and insets, linked text boxes, icons (`a:custGeom`). Not yet: outline caps and arrowhead kinds, adjust handles (`a:avLst` values), presets with inner lines (cube, can, smiley) still come in as rectangles, custom geometry in general (WP20).
- ⬜ **Freeform shapes (`a:custGeom`) and ink** — not read.
- 🟨 **Group shapes and drawing canvas** — groups (`wpg:`), nested, ✅; drawing canvases (`wpc:`) not read (README roadmap).
- 🟨 **VML (`w:pict`, `v:shape`, `v:textbox`)** — only Word's text watermark is read and written (docx.c line 927); VML pictures, text boxes and shapes are not read (README "Not read yet"). WordCraft reads VML pictures, text boxes, lines, rectangles, ovals and presets.
- 🟨 **Alt text** — pictures, shapes, text boxes and groups (`descr`) ✅; the decorative mark (`adec:decorative`) ✅ (WP24); `title` and table alt text (`w:tblCaption`, `w:tblDescription`, WP21b) not read.
- ✅ **Charts (`c:chart`, `cx:chart`) and SmartArt (`dgm`)** [WP15a] — read and kept: each one's parts (the chart part with its embedded workbook, colour and style parts; SmartArt's data, layout, quick style, colour and drawing parts; and the theme their colours refer to, which Quill writes only with them) stay byte for byte with their relationships and ids through a save (core/chartkeep.c, `ObjPack` on `Image.pack`; copies share them, a copy pasted into another document gets new part names and its .rels follows them); Excel 2016's charts are written back inside `mc:AlternateContent` as Word does. Drawn on screen and in the PDF from their cached values (core/chart.c, Ledger's chart layout: column, bar, line, area in their stacked and 100% forms, pie, doughnut, scatter, bubble, radar, stock, combo, and the chartex histogram, waterfall, funnel, treemap, sunburst, box & whisker; other kinds, such as surface, as a frame with the title) and SmartArt from its saved `dsp:drawing` (core/smartart.c: shapes, fills, outlines, text). In line or floating with any wrap, selected, moved and resized as pictures are; `report_lost()` no longer counts them. Checked: tests/test_wp15a.c (41 checks on tests/data/charts.docx: every part byte for byte through two saves, relationship ids, content types, pasting, deleting) and LibreOffice draws Quill's saved file as it draws the original.
- ⬜ **OLE objects (`w:object`)** — dropped (WordCraft shows their picture and saves them back whole).
- 🟨 **Native Word ink (`w14:contentPart`)** — dropped; WordCraft keeps it opaque.
- ✅ **Equations (OMML `m:oMath`)** — WP14a: every ECMA-376 §22.1 element is read into a tree (core/math.c: `m:oMath`, `m:oMathPara` with its justification, fractions bar/skewed/linear/no bar, sub/superscripts and pre-scripts, radicals, n-ary operators with limits, delimiters with separators, functions, accents, bars, group characters, lower and upper limits, matrices with column specs, equation arrays, boxes, border boxes, phantoms, argument sizes, and every property, each kept as absent/on/off), with `m:ctrlPr`, run `w:rPr`, unknown properties and elements (`w:ins` round math...) kept verbatim; written back the same (its m namespace declared on each equation); settings.xml `m:mathPr` kept and used (limits, default justification, small fractions, wrap indent). Laid out TeX-style with Quill's own fonts (core/mathlay.c; math italic, display and text styles, script sizes, TeX spacing) and drawn on screen and in PDFs. Checked: a Word-style sample written from the spec (tests/data/equations.docx, every construct) reads, round trips to identical OMML and a stable document.xml, LibreOffice renders Quill's saved copy exactly as the original, and documents with equations made in Quill open in LibreOffice with them. Left: a display equation too wide for its line isn't broken automatically (only at `m:brk`); block-level `m:oMathPara` outside a paragraph is skipped. WP14b: equations are edited in place (amiga/wp_equation_view.h, core/mathedit.c): the caret inside the tree, the arrow keys through arguments and structures (Up/Down between numerator and denominator, scripts, limits, matrix rows), Home/End, Tab to the next empty argument, typing, Backspace/Delete (a structure selected first, as Word), Shift+arrows and Shift-click selections, Copy/Cut/Paste as linear text, Select All, Esc and Return out, Shift+Return a new line in a display equation, every change one undo step through `doc_set_math`; empty arguments keep a letter's room and show dotted placeholders on the screen; the edited equation is framed.
- 🟨 **Themes, font table, settings, compatibility mode** — (WP13) the theme part is read into `Doc.theme` (core/doctheme.c: names, colour scheme, fonts, effects) and written back byte for byte, or with the parts changed written afresh (Office's theme for a document that had none); theme colours (`w:themeColor`/`themeTint`/`themeShade`, `w:themeFill…`, `a:schemeClr` with lumMod/lumOff/tint/shade) on runs, paragraph shading and borders, cells and shapes and theme fonts (`w:asciiTheme`…) are kept and follow a new scheme; styles' `w:link`, `w:uiPriority`, `w:semiHidden`, `w:unhideWhenUsed`, `w:locked`, `w:hidden`, `w:autoRedefine`, `w:personal`, `w:aliases`, `w:lang`, `w:framePr`, and shortcut keys (customizations.xml) are kept. Compatibility mode 15 is written; embedded fonts (`w:embedRegular`…) not read; latent styles not kept.
- 🟨 **Custom XML, document properties, bibliography sources** — every core property (core.xml), app.xml (company, manager, hyperlink base, statistics; the rest kept verbatim) and custom properties (custom.xml) read and written ✅ (WP24); customXml parts kept verbatim (WP0); bibliography sources: WP3.
- ⬜ **Section text direction (`w:textDirection`)** — not read.
- 🟨 **Section vertical alignment (`w:vAlign`)** — top, center, bottom ✅; `both` (justified) not.
- ⬜ **Ruby (`w:ruby`)** — not read (the base text is kept).
- ⬜ **Document grid (`w:docGrid`, `w:snapToGrid`)** — not read.
- ⬜ **East Asian layout (`w:eastAsianLayout`), `w:fitText`** — not read (WordCraft keeps the first, neither lays them out; `w:fitText` is ➖ for both).
- ⬜ **Glossary document (building blocks, `w:docPart`)** — not read; WordCraft offers them in Quick Parts and AutoText.
- ➖ **`w:altChunk`, sub-documents** — WordCraft doesn't have them either.
- ⬜ **Page background picture (`v:background`/`v:fill`)** — not read; the page colour (`w:background`) ✅.
- ✅ **Document defaults (`w:docDefaults`)** — read and written (docx.c `pPrDefault`/`rPrDefault`).
- 🟨 **Preserving what isn't modelled** — WordCraft keeps unknown parts, settings and custom XML through a save; Quill writes only what it models, so anything else in a file is dropped on save (it warns about tables, pictures, notes and comments it couldn't read, not about content controls, charts' data or settings). Fields are kept since WP1.

### 2.3 Layout

- ✅ **Justification** — `AL_JUSTIFY`.
- 🟨 **Hyphenation** — automatic, Liang patterns by each run's language (core/hyph.c, WP31): British English, German, French, Spanish, Italian, Portuguese, Dutch, Polish (dict/hyph_*.dic from hyph-utf8, MIT/BSD), any other Hyphen .dic in the Dict drawer. WordCraft also has Russian and Turkish (their hyph-utf8 patterns are LPPL, so not shipped; a user's own file works); zone/caps/limit are WP22a's.
- ⬜ **Kerning** — not applied (core/fonts.c measures "without kerning").
- ⬜ **Ligatures and OpenType features** — none (WordCraft shapes text with HarfRust).
- ✅ **Widow/orphan control, keep with next, keep lines together, page break before** — layout.c.
- ⬜ **Bidirectional text (UAX #9), Arabic shaping, right-to-left paragraphs, sections and tables** — none.
- ⬜ **East Asian: vertical text, kinsoku line breaking, document grid, ruby, two lines in one** — none (WordCraft has vertical text, kinsoku, grid and ruby).
- ✅ **Columns** — 1-6 read and offered, equal or of their own widths, line between, balanced before a continuous break (WP22a; balancing is done for equal columns holding only lines: none with tables, paragraph borders or floating objects anchored in them).
- ✅ **Section breaks of each kind, each section with its own page setup and headers/footers**.
- ✅ **Text boxes and shapes with text** — edited in place.
- 🟨 **Wrap types** — square, tight round the outline, top and bottom, behind, in front ✅; through ⬜; wrap on one side only ⬜.
- ✅ **Turned text** — WP18: text in shapes and text boxes turns with them, and turns a quarter for vertical text, on the screen (drawn upright into a bitmap of its own and copied turned, amiga/view.c `draw_text_turned`) and in PDFs.
- ✅ **Line numbering** — start, count by, distance, restart, for each section on its own (WP22a: Apply to in the Line Numbers window).
- ✅ **Drop caps** — dropped or in the margin, 1-10 lines.
- ✅ **Tables across pages** — rows break, heading rows repeat ✅; floating tables ✅ (WP21b: on the page their anchor is on, the text wrapping round them; core/layout.c `place_float_table`, tests "floating table"; a floating table taller than the page runs on rather than breaking as Word's can).
- ✅ **Footnotes** — at the foot of the page that refers to them or beneath its text; a long note continues on the next page under the continuation separator (with the document's continuation notice); endnotes at the document's or each section's end; numbering by format, start, restart each section/page (WP22b).
- ✅ **Tab stops** — left, centre, right, decimal, leaders (at most 8 a paragraph).
- ✅ **Contextual spacing, line spacing rules (auto/at least/exactly)**.
- ✅ **Vertical alignment of the page** — top, centre, bottom, justified (WP22a: the room shared out between the paragraphs; a page in several columns stays at the top).
- ✅ **Page borders** — per section, per side, Word's line kinds, art borders drawn as a repeated square pattern (Word's pictures are not drawn), first page only / all but the first, round the header and footer or not (WP22a).
- ✅ **Watermarks** — text (solid or half transparent) and pictures (scaled, washed out) on screen and in PDF (WP22a).
- ⬜ **Character effects in layout and drawing** — hidden text (and showing it with marks), outline/shadow/emboss/engrave, enclosed characters, WordArt transforms.
- ✅ **Equations, charts, SmartArt drawn on the page** — charts and SmartArt ✅ [WP15a] (see 2.2); equations ✅ (WP14a: TeX build-up rules with Quill's fonts, glyphs the fonts lack drawn as outlines or from look-alike glyphs, growing delimiters, radicals, braces and arrows drawn; inline equations in the line, display ones on lines of their own as their justification says; on screen from the fonts' outlines, in PDFs as text or outlines).
- ✅ **Metric-compatible fonts** — Liberation, Carlito, Caladea measured from their TrueType metrics, as WordCraft does (WordCraft also ships its own substitutes for more families).

### 2.4 Editing

- 🟨 **Find and Replace** — match case, whole words, backwards, replace one or all (`QFind`, `FIND_CASE`/`FIND_WORD`/`FIND_BACK`). Missing: find and replace formatting and styles, special characters (^p, ^t, ^m, ^^), wildcards / pattern matching (WordCraft cmd/find_pattern.rs), find in the selection, Highlight All, Find in headers, notes and comments.
- ✅ **Navigation pane search results** — the Results tab lists each match with its context (panes.c).
- ✅ **AutoCorrect** — core/autocorrect.c (WP8): the replacement list (symbols and common slips, English text only), TWo INitial CApitals, sentence capitals, table cells, days, cAPS LOCK, first-letter and initial-caps exceptions, the AutoCorrect submenu on a misspelt word; each correction its own undo step, tracked when tracking.
- ✅ **AutoFormat as you type** — core/autocorrect.c (WP8): smart quotes, en and em dashes, ½ ¼ ¾, superscript ordinals, `* `/`- `/`> ` bulleted and `1. `/`a) ` numbered lists (at their number), border lines from `---` `===` `___` `***` `~~~`, links from web and mail addresses, *bold* and _italic_; Format > AutoFormat Now does the same to the selection or the document (one undo step). Return in an empty list item ends the list, Tab/Shift-Tab change the level.
- ➖ **AutoComplete** — WordCraft doesn't have it either.
- 🟨 **Spelling** — British English (SCOWL word list, core/spell.c) and any language with a Hunspell dictionary in the Dict drawer (core/hunspell.c, WP31), each run by its language; wavy lines as you type (switchable: Review > Proofing), suggestions ranked by typing errors, Change/Change All/Ignore/Ignore All/Add/AutoCorrect, your own words in Quill.words, right-click suggestions, text marked "do not check". Missing: the "ignore words in capitals / addresses" options (words with digits are never flagged).
- ✅ **Grammar** — core/grammar.c (WP8), English: repeated word, a/an (by sound: an hour, a unicorn, an FBI), sentence capital (abbreviations known), space before punctuation and none after, two spaces, commonly confused words (could of, their is, more then...); optional style checks (passive voice, wordy phrases). Blue double line as you type (dotted gold for style), in the Spelling and Grammar window and on the right-click menu. Other languages' grammar rules missing (WordCraft has a few per language).
- ✅ **Thesaurus** — core/thesaurus.c (WP8): MyThes .dat thesauri (dict/th_en.dat made from WordNet, 36,834 words; an installed th_xx_YY.dat for other languages), base forms looked up (cities, stopped), the Thesaurus window and Synonyms on the right-click menu.
- ✅ **Word count** — pages, words, characters with and without spaces, paragraphs, lines (`word_count()`); live count on the status bar.
- 🟨 **Read Aloud** — WP8: translator.library and narrator.device, a sentence at a time; a message if they are missing. Untested speaking (no narrator.device in the test emulator); no voice or speed settings.
- ➖ **Dictate** — speech recognition service; WordCraft doesn't have it either.
- ➖ **Translate** — online service; WordCraft doesn't have it either.
- ➖ **Smart Lookup / Researcher** — online service; WordCraft only reports that it needs one.
- 🟨 **Navigation pane** — Headings (current one marked, click to go), Pages (thumbnails), Results ✅. Missing: drag a heading to move its section, collapse/expand headings, promote/demote from the pane.
- ⬜ **Outline view** — see the Outlining tab.
- ✅ **Focus mode** — `ID_FOCUS`.
- 🟨 **Immersive Reader** — WordCraft's is Read Mode; Quill's Read Mode ✅, but no column width, page colour, text size or line spacing choices in it (WordCraft `reading_toolbar`).
- 🟨 **Zoom** — list, slider 10-500%, one page, page width, multiple pages ✅; no Zoom dialog with "many pages" choices.
- ⬜ **Track changes options** — WordCraft: markup display modes, balloons, colours by author, moves, formatting tracking. Quill: one colour scheme, inline only.
- ⬜ **Compare and Combine documents** — WordCraft cmd/review (word-level diff into tracked changes).
- ⬜ **Protect document / Restrict Editing** — WordCraft cmd/protect (read-only, comments only, tracked changes only, forms, exceptions, password hash).
- ➖ **Macros and scripts** — left out on purpose (WordCraft records and plays back commands, `tools.macros`, `tools.recordMacro`; it keeps but never runs VBA).
- ➖ **Add-ins** — WordCraft has none either (Get Add-ins is an online store).
- ✅ **Accessibility checker** — core/inspect.c `acc_check`, the Accessibility pane (WP24).
- ✅ **Document inspector** — core/inspect.c (comments, revisions, properties, headers, custom XML; hidden text once WP9 has it) (WP24).
- ✅ **AutoSave, recovery copies, version history** — wp_info.c and core/recover.c (PROGDIR:Recovery, PROGDIR:Versions) (WP24).
- 🟨 **Templates** — five built-in templates on the File page (core/template.c). Missing: reading user .dotx templates as templates (a new untitled document from them), Save as Template, a personal templates folder, attaching a template.
- ➖ **Printing and print preview** — left out on purpose: PDF export instead (Quill has an untested printer.device path, `print_doc()`).
- ✅ **Several documents at once** — up to eight windows (`win_new()`).
- 🟨 **Undo and redo** — ✅, typing undone a word at a time; no undo list, no Repeat (F4).
- 🟨 **Clipboard** — Quill's own formatted clipboard between Quill documents, text through clipboard.device to other programs (amiga/clip.c). Missing: formatted paste from other programs (WordCraft takes RTF/HTML from the system clipboard), pasting pictures, the Clipboard pane.
- 🟨 **Format painter** — `ID_BRUSH` one shot; no double-click to keep it on, no Ctrl+Shift+C/V.
- ⬜ **Spike, Go Back, Extend Selection (F8), column (block) selection, Alt+X** — see the Editing tab.
- 🟨 **Right-click menus** — Quill (`ID_CTXPICK`): misspelled word, picture/shape/group, text (with table Insert/Delete/Merge/Split/Properties), link, field (Update Field, Edit Field, Toggle Field Codes: WP1). Synonyms ✅ (WP8, with grammar suggestions and an AutoCorrect submenu). Missing against WordCraft: Paste options on the text menu, table of contents menu (Update, Remove), picture menu's Change Picture, Reset Picture, Insert Caption, table menu's Distribute, AutoFit, Text Direction and Insert Caption, equation menu. (the equation menu has Edit Equation... since WP14a)

### 2.5 Interface: panes, toolbars and the window

- ✅ **Ribbon with contextual tabs, split buttons, galleries, bubble help** — amiga/ribbon.c. (Quill also has an Amiga menu bar, and a Header & Footer and an Edit Text contextual tab where WordCraft puts those tools on Insert.)
- ✅ **Quick access buttons** — Save, Undo, Redo on the tab strip (`g_undo`); not customizable.
- ⬜ **Ribbon collapse** — WordCraft `ui.collapseRibbon`; Quill only hides everything with Focus.
- ⬜ **KeyTips (Alt/F10 letters on the ribbon)** — WordCraft crates/ui-egui/src/keytips.
- ⬜ **Mini toolbar on a selection** — WordCraft crates/ui-egui/src/mini_toolbar.
- ⬜ **Command search (Tell Me, Alt+Q)** — WordCraft `tools.search`.
- ✅ **Status bar** — page, words, language (the caret's: WP31, a click opens the Language window), Focus, view buttons, zoom slider (panes.c).
- ✅ **Rulers** — horizontal and vertical, indents, tab stops, table column edges, margins.
- ✅ **Navigation pane** — Headings, Pages, Results.
- ✅ **Comments** — balloons beside the page with replies in their thread's balloon and resolved threads dimmed; the Comments pane (WP5 `ID_RV_CPANE`, a window) lists the threads with their replies, with Reply, Resolve and Delete.
- 🟨 **Styles pane** — a Styles window (Manage); not docked, no previews, no Style Inspector or Reveal Formatting (WordCraft panes/inspector.rs).
- ✅ **Selection pane** — `open_selpane()` (a window).
- 🟨 **Alt Text pane, Format Shape pane, Reviewing pane, Accessibility pane, Restrict Editing pane, Clipboard pane** — Alt Text and Accessibility panes ✅ (WP24, windows that stay open and follow the selection); the Reviewing pane ✅ (WP5, a window: see Review › Reviewing Pane); the others none.
- ⬜ **Alignment guides when moving objects** — WordCraft crates/ui-egui/src/guides.rs.
- ✅ **Start page** — File page Home with templates and recent documents (backstage.c).
- ✅ **Dark interface and dark page** — `ID_THEME`, `ID_DARKPAGE`.
- ✅ **Interface text size** — Normal/Large (WordCraft has a size scale).
- ➖ **Caret animation** — WordCraft's gliding caret is cosmetic and costly on a 68040.
- ➖ **"Make default for Word documents"** — OS registration; on the Amiga the installer's icons cover it.

### 2.6 The Options dialog

WordCraft's Options has eight panes (crates/ui-egui/src/options.rs). Quill's Options is one page on the File page (amiga/backstage.c `BP_OPTIONS`) plus Window and screen (`open_winprefs()`) and Quill.prefs keys (`hinting=off`, `aatext=off`).

- 🟨 **General** — WordCraft: mini toolbar, dark page, community button, start screen, user name and initials, interface theme, interface size. Quill: user name ✅, theme ✅, text size ✅, dark page (View) ✅; initials ⬜ (comments get them only from files), start screen choice ⬜, mini toolbar ➖ (no mini toolbar).
- ⬜ **Display** — which formatting marks show even when ¶ is off (tabs, spaces, paragraph marks, hidden text), show rulers by default, print page colour, update fields before printing. Quill: rulers are remembered (`ID_RULER`), the rest ⬜ (printing ones ➖).
- 🟨 **Proofing** — AutoCorrect Options ✅, check spelling as you type and mark grammar errors on/off ✅ (Review > Proofing, PROGDIR:Quill.proof), style checks ✅ (Spelling and Grammar window), which languages have a dictionary ✅ (the Language window). Missing: ignore capitals/addresses, a custom dictionary editor (Quill.words is a text file).
- 🟨 **Save** — AutoSave ✅ (File › Options, WP24); personal information removed through Inspect Document ✅, not automatically on each save ⬜.
- 🟨 **Language** — interface language ✅ (5 languages); proofing language ✅ (WP31: the Language window's Set As Default, PROGDIR:Quill.lang); region ⬜.
- ⬜ **Accessibility** — animation effects, Read Aloud voice and speed, skip citations.
- 🟨 **Advanced** — measurement units: Quill cm/in ✅ (WordCraft also mm, pt, pi); alignment guides ⬜; recent documents count ⬜ (Quill keeps a fixed number); region and decimal separator ⬜; KeyTips ⬜; caret animation ➖; user templates folder ⬜; customize keyboard and ribbon ⬜.
- ➖ **Agents** — MCP and control channel: left out on purpose.
- ✅ **Window and screen (Quill only)** — public screen and window placement; nothing to match in WordCraft.

### 2.7 Keyboard shortcuts

WordCraft binds 126 commands to keys (counted from the `.key(...)` of every `CommandSpec` in crates/engine/src); Quill has about 45 (README "Keys", menu shortcuts in main.c `menus[]`, view.c `key()`). Amiga conventions differ (Amiga-L selects all, Amiga-A is Save As, Ctrl-Left/Right go to the line's ends, Alt-Left/Right by words), so this lists functions, not identical keys.

- ✅ **Present in Quill:** cursor movement by character, word (Alt), line end (Ctrl or Home/End), paragraph (Alt-Up/Down), screen, document; Backspace/Del and by word; Return, Shift-Return; Tab and Shift-Tab (lists, cells); Esc; New, Open, Save, Save As, Close, Quit, Next Window; Export PDF; Undo, Redo, Cut, Copy, Paste, Select All; Find, Find Next; Bold, Italic, Underline, Superscript, Subscript; Align left, centre, right, justify; single, 1.5 and double spacing; indent and outdent; page break; grow and shrink font; formatting marks; spelling; insert table; edit header; fit page width.
- ⬜ **Missing (by WordCraft command):** Copy/Paste Formatting (`edit.copyFormat`/`edit.pasteFormat`), Paste Special (`edit.pasteSpecial`), Paste Text (`edit.pasteText`), Repeat F4 (`edit.repeat`), Go Back Shift+F5 (`edit.goBack`), Go To F5 (`edit.goto`, the command exists without a key), Spike and Insert Spike (Ctrl+F3, Ctrl+Shift+F3), field keys (done by WP1: F9 update, Shift+F9 toggle, Ctrl+F9 the Field window, Alt+F9 all codes, Ctrl+Shift+F9 and Ctrl+6 unlink, Ctrl+3/Ctrl+4 lock/unlock, F11/Shift+F11/Ctrl+F11/Ctrl+Shift+F11 where the keyboard sends F11), Change Case Shift+F3, Double Underline, Word Underline, Small Caps, All Caps (command exists without a key), Strikethrough (exists without a key), Hidden, Font dialog Ctrl+D (exists without a key), font name and size boxes (Ctrl+Shift+F/P), grow/shrink by 1 pt (Ctrl+]/[), Bookmark Ctrl+Shift+F5, Link Ctrl+K, outline keys (Alt+Shift+1-9, A, +, -), Add Space Before Ctrl+0, Bullets Ctrl+Shift+L, Distributed Ctrl+Shift+J, Hanging Indent Ctrl+T and Remove Hanging Ctrl+Shift+T, Heading 1-3 Ctrl+Alt+1-3, Normal Ctrl+Shift+N, Footnote Ctrl+Alt+F, Read Aloud, Thesaurus Shift+F7, Track Changes Ctrl+Shift+E, Column Selection Ctrl+Shift+F8, Extend Selection F8, Style Inspector Shift+F1, Styles pane, Column Break Ctrl+Shift+Return, © ® ™ … — (Ctrl+Alt+C/R/T/., Ctrl+Alt+-), Nonbreaking Hyphen and Space, Optional Hyphen, Draft/Outline/Print Layout views (Ctrl+Alt+N/O/P), Split Ctrl+Alt+S, Macros Alt+F8 (➖), Search Alt+Q, Move paragraph up/down and promote/demote (Alt+Shift+arrows).
- ⬜ **Customize Keyboard** — WordCraft keeps a user key map (engine/src/keymap.rs). Quill none.

### 2.8 Interface and proofing languages

- 🟨 **Interface languages** — WordCraft offers 27 (en, zh-hans, zh-hant, ja, ko, uk, es, pt-br, sr, sr-latn, pl, cs, et, de, ru, tr, it, hu, fr, bg, vi, sv, hr, nl, da, ro, sl; crates/ui-egui/src/i18n.rs, catalogs in storytold/craft-i18n). Quill offers 5: English, German, French, Spanish, Italian (amiga/lang.c `ui_languages`, amiga/lang_table.h). Missing 22. Quill's interface fonts are Latin-1, so Dutch, Swedish, Danish and Brazilian Portuguese need only translations. Polish, Czech, Croatian, Hungarian, Slovenian, Romanian, Estonian, Serbian (Latin) and Turkish need Latin-2 (or Latin-5/Latin-4) screen fonts. Russian, Ukrainian, Bulgarian and Serbian need Cyrillic fonts. Chinese, Japanese, Korean and Vietnamese are out of reach of the current font pipeline.
- 🟨 **Menus follow the language change only after a restart** — README 0.2 (WordCraft changes everything at once).
- 🟨 **Proofing languages** — Quill (WP31): a language on each run (`w:lang`, its eastAsia and bidi kept; `w:noProof`), the Language window, the status bar showing the caret's language; British English built in and any Hunspell dictionary (.aff/.dic: SET, FLAG char/long/num/UTF-8, AF, PFX/SFX with conditions, cross products, two suffixes, NEEDAFFIX, FORBIDDENWORD, KEEPCASE, ONLYINCOMPOUND, compounds with COMPOUNDFLAG/BEGIN/MIDDLE/END, TRY/REP suggestions; checked against LibreOffice's en_US, de_DE_frami, es_ES, it_IT, pt_PT, nl_NL and pl_PL); hyphenation for eight languages. WordCraft: the same Hunspell idea, grammar rules per language and Russian/Turkish hyphenation, which Quill lacks (grammar is English only).

### 2.9 Zotero

- ➖ **Zotero integration** (WordCraft crates/zotero, the Zotero tab, docs/zotero.md) — talks to the Zotero desktop app through its local HTTP connector; there is no Zotero for AmigaOS. What Quill should still do is keep Zotero's and Mendeley's `ADDIN ZOTERO_ITEM`/`ADDIN ZOTERO_BIBL` fields and their results through a save, so a document can go back to a machine with Zotero: WP1 does (tests/test_wp1.c).

### 2.10 Control protocol, MCP, command line, scripting

- ➖ **MCP server (`wordcraft-cli mcp`), JSON control channel (`--control <port>`), CLI (convert, parity, …), macro recording** — left out on purpose. (Quill has its own ARexx port, `QUILL.1`, with 30-odd commands; also left out of this comparison.)

### 2.11 Network and cloud features (left out on purpose)

- ➖ Online Pictures, Online Video, Get Add-ins, Researcher, Translate, Dictate, Block Authors (co-authoring), Check for Updates, Community Discord, Share to the cloud, Zotero (needs the desktop app). WordCraft implements none of the online services itself either, except Check for Updates and the Discord link.

## WP0 conventions

WP0 (the scaffolding, branch wp0) lets the packages above be built side by side on their own branches. The rule behind all of it: **a package writes in files of its own, and where it must touch a shared file it adds lines right after a marker line of its own, never editing the marker or anyone else's lines.** Git merges insertions that are separated by an unchanged line, so packages that each insert after their own marker merge without conflicts. The short version is at the top of amiga/wphooks.h.

**The package table.** 39 packages, in this order (their index `WPI_n` in amiga/ids.h): WP1 fields, WP2 refs, WP3 cite, WP4 mailings, WP5 review, WP6 compare, WP7 protect, WP8 proof, WP9 charfmt, WP10 para, WP11 outline, WP12 find, WP13 styles, WP14a math, WP14b equation, WP15a chartkeep, WP15b chart, WP15c smartart, WP16 controls, WP17 picture, WP18 shapes, WP19 wordart, WP20 draw, WP21a tables, WP21b tables2, WP22a pages, WP22b notes, WP23 templates, WP24 info, WP25 windows, WP26 custom, WP27 intl, WP28a rtf, WP28b odt, WP28c html, WP28d docbin, WP29 pdf, WP30 uilang, WP31 proflang. The name is the package's Amiga file, amiga/wp_<name>.c, which exists already with an empty `wpN_command()` and its `wpN_package` (WP14a's are `wp14a_command`, `wp14a_package`, and so on).

**Command ids.** Each package owns 64 ids: `ID_WPn_FIRST` .. `ID_WPn_LAST` (ids.h), from 4000 up (`ID_WP1_FIRST` = 4000, WP2 4064, ... WP31 ends at 6495; `ID_WP_FIRST`/`ID_WP_LAST` span them all). They stay clear of the notification ids at 1000, the window ids at 3000 and 3100 and of every range test in main.c, and fit the 16 bits of the right-button menu's user data. Name them in your own file: `enum { ID_FIELD_INSERT = ID_WP1_FIRST, ID_FIELD_UPDATE, ... }`.

**Commands.** main.c's `command()` and `ribbon_choice()` send every id of a block to its package's `int wpN_command(ULONG id, LONG value)` (through wphooks.c's `wp_run`). `value` is the entry chosen when the id is one of the package's ribbon lists (a `K_DROP` or `K_FIELD` item in its groups, which the main loop recognises with `wp_is_list`) or a `WP_CTX_LIST` context submenu; otherwise `WPV_NONE` (-1). Return `WPR_KEEPFOCUS` if the command opened a window whose gadgets should keep the keyboard, else `WPR_DONE`. main.c exports what packages need for that: `wp_app()`, `wp_win()`, `wp_view()`, `wp_ribbon()`, `wp_doc()`, `wp_caret()`, `wp_ask()`, `wp_dialog()` (a dialog whose OK returns one of your ids), `wp_file_request()`, `wp_latin1_utf8()`, `wp_story_close()`, `wp_refresh()`.

**Ribbon groups.** Fill `wpN_package.groups` with `WpGroup` rows `{ TAB_x, before, { "Caption", G_COLS, items, n } }` (amiga/ribbondef.h): the group goes into tab `TAB_x` before that tab's own group number `before`, or at the end with -1; several packages adding to one tab come in package order. The tabs, in WordCraft's order: File, Home, Insert, **Draw**, Design, Layout, References, **Mailings**, Review, View, **Developer**, then the contextual Table Design, Table Layout, Picture Format, Shape Format, Header & Footer, Edit Text, **Equation**, **Chart Design**, **SmartArt Design**, **Outlining**. The new ones (bold) have no groups of their own and stay out of the strip until a package gives them one, so the WP0 build looks exactly like 0.14. Items are ribbon.c's `Item` with three fields at the end for packages: `xlist` (a `RibbonList` of your own; leave `list` 0), `xicon` (a 16×16 drawing in theme.c's icon language, used when `icon` is 0, so you need not edit theme.c), `split` (a split button's direct command). A `K_FIELD` of yours shows the text your `field` hook writes. Main.c's literal tab numbers became `TAB_` names, since the new tabs moved the indices. When more tabs are shown than fit the window, the strip now narrows the room round the names (16 down to 6 pixels) and then cuts the longest names short with a dot, so it always fits 640 pixels; with only 0.14's tabs nothing changes.

**Ribbon states.** Each package has a 32-bit state word of its own (`MUIP_Ribbon_State.wpon[WPI_n]`), set by its `state(ULONG *on, const WpCtx *c)` hook every time the view sends the ribbon its state (caret moves, edits, `wp_refresh()`). Use `RB_WP(WPI_n, bit)` as an item's `st` (shown checked) or `en` (needed to be enabled). Bit `WPS_TAB` (0) shows the package's contextual tab: WP11 Outlining, WP14b Equation, WP15b Chart Design, WP15c SmartArt Design. `Item.st`/`en` are 16 bits now.

**Menus.** Fill `wpN_package.menus` with `WpMenu` rows `{ WM_x, { NM_ITEM, "Label", "key" or 0, flags, 0, (APTR)id } }`; `wp_menus()` adds them at the end of that menu (File, Edit, Insert, Format, Table, View, Review, Layout), in package order, translated like the others. Start with an `NM_BARLABEL` row; prefer one `NM_ITEM` with `NM_SUB` rows when there are several (Intuition allows 63 items a menu, 31 sub-items). No ninth title: measured with the menu bar's topaz 8 (8 pixels a character, 8 between titles), the eight titles take 396 pixels in English, 540 in German, 620 in French, 580 in Spanish and 596 in Italian, and a "Tools" title (Extras, Outils, Herramientas, Strumenti) would need 48-104 more: past 640 in French, Spanish and Italian. (French, Spanish and Italian are this long partly because the Format menu takes the Format Painter's translation, "Reproduire"/"Copiar formato"/"Copia formato", from the same table row: a small fix for WP26 or WP30.)

**Right-button menu and keys.** A package's `ctxmenu(Object *menu, const WpCtx *c)` hook adds items with `wp_ctx_item(menu, label, WP_CTX_ID(id), enabled)` or a submenu with `wp_ctx_sub(menu, label, id, labels, n)` (each entry runs id with its number); `c->where` says what was clicked (`WPC_TEXT`, `WPC_SPELLING`, `WPC_LINK`, `WPC_OBJECT` with `c->obj`). Its `key(code, qual, c)` hook sees each raw key before the view's own handling (not in read mode, not with the Amiga keys, which belong to menu shortcuts) and returns one of its ids to run, or 0. `WpCtx` carries the document, caret, selection end, whether in a table, the view mode and the selected object.

**Words.** Each package's translations live in its own tools/lang/wp<n>.py (a `W` list like tools/mklang.py's, Word's own terms, Latin-1); `python3 tools/mklang.py` writes them into the package's section of amiga/lang_table.h, between `/* ---- begin WPn */` and `/* ---- end WPn */`, so two packages' rows never touch. If two branches both add the same string, run mklang.py again after merging. `python3 tools/langcheck.py amiga/wp_<name>.c` lists strings that have no row (and rows whose translations lose a `%s` or a `|`); `--rows` prints them ready to paste.

**Tests.** tests/test_wp<n>.c has an empty `test_wp<n>()`, called from coretest.c's `main()`; tests/testutil.h shares `ok()`, `P()`, `all()`, `text_is()`, `runs_sane()`, `same_doc()`, `docx_again()` (save and read back) and the test fonts `fs`. build.sh compiles every core/*.c and tests/test_wp*.c (new files need no line there) into a test binary named after the worktree, so parallel builds don't overwrite each other's. WP0's own checks are in tests/test_wp0.c.

**File formats.** core/docx.c's `doc_formats[]` is the table `doc_load_file()`, `doc_save_file()`, Save and the Open/Save As requesters' patterns (`doc_format_pattern`, e.g. `#?.(docx|doc|txt|text|asc)`) are built from: ODT, .docx, .doc, RTF, HTML, Markdown, plain text, each a `DocFormat` (name, endings, `sniff` of the first bytes, `load`, `save`, `DF_NATIVE` for "Save writes back to it", `DF_TEXT` for the fallback). Reading tries each row's sniff in order, then the name's ending, then plain text. WP28a-d's rows are there already, defined in their own files (core/rtf.c, core/odt.c, core/html.c, core/markdown.c, core/docbin.c) with NULL functions, which leaves them out; a package fills in its row and nothing else. The .doc refusal moved to core/docbin.c's `load`.

**.docx hooks.** core/docx.c has hook functions with one marker line for each package that touches .docx: `docx_read_block_ext` (a body-level element Quill doesn't read), `docx_read_elem_ext` (one inside a paragraph), `docx_read_setting_ext` (each element of settings.xml), `docx_read_parts_ext` (after everything is read: other parts), `docx_write_parts_ext` (parts, content types and relationships through `docx_out_part()`) and `docx_write_settings_ext` (settings.xml at the four places the schema's order allows: first; before `w:defaultTabStop`; after `w:autoHyphenation`; after `w:compat`). Write your own `read_*`/`write_*` function (in docx.c next to the hooks, or in a core file of your own when it needs only public types) and put one call to it on a new line right after your marker (the 27 packages whose files list core/docx.c have markers; another one adds its own marker line, then its call, after the last marker). New Doc fields go at the end of `struct Doc` (core/doc.h has a comment there).

**Parts kept verbatim.** core/keep.c keeps .docx parts Quill doesn't read and writes them back with their relationships and content types (`Doc.kept`): custom XML (with its own .rels and item properties), the glossary document, people.xml (until WP5, which reads and writes it), and the package's extended and custom properties. The thumbnail and digital signatures are dropped on purpose (they would be wrong after an edit). A package that starts reading such a part calls `keep_remove()`; one that keeps a part the text refers to (a chart, say) calls `keep_add()` and writes the relationship id it returns (ids Quill uses itself are never handed out). `xml_raw()` (core/xml.h) gives an element's exact text, for keeping elements verbatim inside a package's own model. Checked: word.docx and lo.docx keep all their custom XML and property parts byte for byte through two saves, and LibreOffice opens the result unchanged.
