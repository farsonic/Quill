# Quill against WordCraft: what is missing

WordCraft (github.com/storytold/wordcraft, the Rust word processor, MIT OR Apache-2.0) is the program Quill follows. This list was made on 2026-10-29 from WordCraft's latest source: its feature catalogue (crates/engine/src/catalog.rs, the 520 ribbon and menu commands its `cargo xtask parity` checks), the ribbon itself (crates/ui-egui/src/ribbon/, which adds the Zotero and Help tabs: 11 more), its commands (crates/engine/src/cmd), its options, key bindings and languages, and its docs (file-format-parity.md, layout-parity.md, typing-parity.md, localization-parity.md, target-app-parity.md, gaps.md, parity-checklist.md). Quill 0.14 was checked command by command in its source: every ✅ cites the id (amiga/ids.h) or function that does it; a ⬜ was grepped for and not found.

Marks: ✅ Quill has it · 🟨 partly (what is missing is said) · ⬜ missing · ➖ left out on purpose (printing, as Quill exports PDF; ARexx/MCP/control protocols and macros; anything needing a network or cloud service), or WordCraft doesn't have it either (its catalogue lists 15 commands it hasn't built; they are ➖ here too). `[WPn]` after a missing item names the work package below that closes it.

## Summary

- **Ribbon commands:** 531 in WordCraft (the 520 of its catalogue plus Zotero and Help), 500 of them applicable to Quill. Quill has **314** (✅), **12** partly (🟨) and lacks **174** (⬜); 31 are ➖. So Quill covers 63% of the applicable commands fully and 65% at least in part.
- **Beyond the ribbon (section 2):** 152 features: ✅ 48, 🟨 43, ⬜ 44, ➖ 17.
- **Strongest:** Home (formatting, lists, styles), Table Layout, Header & Footer, Layout, Design, the views and the core editing keys.
- **Missing whole:** Mailings (22), the Equation tab and equations (21), Outline view and Outlining (14), Developer/content controls (11), Draw/ink (10), SmartArt (8) and charts (6), and almost all of References beyond the TOC and notes (citations, captions, index, authorities).
- **Biggest risks for documents people send:** fields other than page numbers and cross-references lose their codes on save; content controls, charts, SmartArt, equations, OLE objects and VML pictures are dropped (formatting revisions are kept since WP5); no AutoSave or recovery (WP24 adds them). These are in priority 1 below.
- **Other formats:** Quill reads and writes .docx and plain text and exports PDF; WordCraft also reads .doc and reads/writes .rtf, .odt, .html, .md, .tex and Word XML (WP28a-d).

| Tab | Commands | ✅ | 🟨 | ⬜ | ➖ |
|---|---|---|---|---|---|
| File | 23 | 16 | 1 | 4 | 2 |
| Quick Access | 5 | 5 | 0 | 0 | 0 |
| Title Bar | 1 | 0 | 0 | 1 | 0 |
| Home | 105 | 96 | 2 | 6 | 1 |
| Insert | 53 | 32 | 1 | 14 | 6 |
| Draw | 11 | 0 | 0 | 10 | 1 |
| Design | 10 | 9 | 0 | 1 | 0 |
| Layout | 22 | 19 | 0 | 3 | 0 |
| References | 25 | 8 | 2 | 14 | 1 |
| Mailings | 23 | 0 | 0 | 22 | 1 |
| Review | 31 | 21 | 1 | 7 | 2 |
| View | 29 | 18 | 2 | 4 | 5 |
| Read Mode | 3 | 0 | 0 | 2 | 1 |
| Developer | 11 | 0 | 0 | 11 | 0 |
| Zotero | 7 | 0 | 0 | 0 | 7 |
| Help | 4 | 1 | 0 | 0 | 3 |
| Table Design | 8 | 4 | 0 | 4 | 0 |
| Table Layout | 33 | 26 | 1 | 6 | 0 |
| Picture Format | 14 | 14 | 0 | 0 | 0 |
| Shape Format | 13 | 9 | 0 | 4 | 0 |
| Header & Footer | 5 | 5 | 0 | 0 | 0 |
| Equation | 22 | 1 | 0 | 20 | 1 |
| Outlining | 13 | 0 | 0 | 13 | 0 |
| SmartArt Design | 7 | 0 | 0 | 7 | 0 |
| Chart Design | 5 | 0 | 0 | 5 | 0 |
| Editing | 41 | 29 | 2 | 10 | 0 |
| Tools | 7 | 1 | 0 | 6 | 0 |
| **All** | **531** | **314** | **12** | **174** | **31** |

| Area | ✅ | 🟨 | ⬜ | ➖ |
|---|---|---|---|---|
| 2.1 File formats | 5 | 5 | 6 | 3 |
| 2.2 DOCX features | 8 | 15 | 14 | 1 |
| 2.3 Layout | 16 | 3 | 5 | 0 |
| 2.4 Editing | 7 | 11 | 9 | 7 |
| 2.5 Interface: panes, toolbars and the window | 10 | 2 | 5 | 2 |
| 2.6 The Options dialog | 1 | 4 | 3 | 1 |
| 2.7 Keyboard shortcuts | 1 | 0 | 2 | 0 |
| 2.8 Interface and proofing languages | 0 | 3 | 0 | 0 |
| 2.9 Zotero | 0 | 0 | 0 | 1 |
| 2.10 Control protocol, MCP, command line, scripting | 0 | 0 | 0 | 1 |
| 2.11 Network and cloud features (left out on purpose) | 0 | 0 | 0 | 1 |
| **All** | **48** | **43** | **44** | **17** |

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
- **WP8 Proofing: grammar, thesaurus, AutoCorrect, AutoFormat, Read Aloud** (ribbon 4; 4-6 h). Grammar checks for English (repeated word, a/an, sentence capital, space before punctuation, double space), Spelling & Grammar window; a thesaurus from a word list with a compatible licence, also on the right-click menu; AutoCorrect with Options (replacement list, two initial capitals, sentence capitals, days, Caps Lock, exceptions); AutoFormat as you type (smart quotes, dashes, fractions, ordinals, `* `/`1. ` lists, `---` lines, links); Read Aloud through translator.library and narrator.device. Files: core/spell.c, new core/grammar.c, new core/autocorrect.c, new core/thesaurus.c, dict/ (data and licences), amiga/view.c (typing hook), new amiga/wp_proof.c.
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
- **WP19 WordArt and signature line** (ribbon 6; 3-4 h; after WP9 and WP18). WordArt text effects (`w14:textFill`, `textOutline`, `shadow`, `glow`, `reflection`), Insert WordArt, Quick Styles, Text Fill, Text Outline, a few Transform warps; Signature Line. Files: core/doc.h, core/docx.c, amiga/view.c, core/pdf.c, new amiga/wp_wordart.c.
- **WP20 Draw tab and drawing canvas** (ribbon 12; 4-6 h). Pens, pencil, highlighter, eraser, lasso, Add Pen, Ink to Shape, Ink Replay, Hide Ink; ink saved as freeform shapes (`a:custGeom`, which also reads Word's freeforms); Drawing Canvas (`wpc:`, README roadmap). Files: core/doc.h (OBJ_INK, OBJ_CANVAS), core/docx.c, amiga/view.c (pen input), core/pdf.c, new amiga/wp_draw.c, amiga/ribbon.c (its tab from WP0).
- **WP26 Customization and discoverability** (ribbon 4; 4-5 h). Options in panes (General, Display, Proofing, Save, Language, Advanced) for the settings the other packages add; Customize Ribbon and the quick access buttons; Customize Keyboard (a key map in Quill.prefs); command search (Tell Me); KeyTips; a mini toolbar; the missing keyboard shortcuts of section 2.7 where Amiga keys allow. Files: amiga/ribbon.c, amiga/main.c, amiga/backstage.c, new amiga/wp_custom.c.
- **WP27 Right-to-left and East Asian** (ribbon 9; 8+ h, split into bidi and East Asian). UAX #9 bidi, right-to-left paragraphs, sections and tables, Arabic and Hebrew shaping, numerals; vertical text, document grid, Asian typography, ruby (Phonetic Guide). Needs fonts and an interface encoding beyond Latin-1; lowest priority for an Amiga word processor. Files: core/layout.c, core/doc.h, core/docx.c, core/fonts.c, core/ttraster.c, amiga/view.c.
- **WP28b ODT** (beyond-ribbon 1; 4-5 h). Read and write .odt (styles, lists, tables, pictures, headers and footers, notes). Files: new core/odt.c (core/zip.c already reads deflated entries), a row in the format table.
- **WP28d Word 97-2003 .doc reader** (beyond-ribbon 1; 8-12 h, two agents: text/formatting/styles, then tables/pictures/notes/fields). Files: new core/docbin.c, core/cfb.c (from WP7, or written here first).
- **WP30 More interface languages** (beyond-ribbon 1; 2-3 h for Latin-1 ones). Dutch, Swedish, Danish, Brazilian Portuguese first (Latin-1 fonts suffice); then Latin-2 screen fonts (tools/mkamigafonts.py) for Polish, Czech, Hungarian, Croatian, Slovenian, Romanian. Files: amiga/lang_table.h, amiga/lang.c, tools/mklang.py, tools/mkamigafonts.py, fonts-amiga/.
- **WP31 Proofing languages** (ribbon 2; 4-6 h). A language on each run (`w:lang`, read and written; Review › Language and Set Proofing Language), a Hunspell .aff/.dic reader so installed dictionaries work, more hyphenation patterns (German, Italian, Portuguese, Polish with compatible licences). Files: core/spell.c, core/hyph.c, core/doc.h (CharFmt.lang), core/docx.c, dict/, amiga/panes.c (the language on the status bar).

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

### File (23: ✅ 16 · 🟨 1 · ⬜ 4 · ➖ 2)

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
- ⬜ Save as Template (Backstage) `file.saveTemplate` — no .dotx writing [WP23]
- ⬜ Personal Templates (Backstage) `file.listTemplates` [WP23]

### Quick Access (5: ✅ 5 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Undo (Toolbar) `edit.undo` — `ID_UNDO` (Amiga-Z, tab strip button)
- ✅ Undo List (Toolbar) `edit.undoTo` — Edit › Find and Select › Undo List... / Redo List... (`ID_FX_UNDOLIST`, wp_find.c): the steps named by `doc_undo_step()` (Typing, Delete, Formatting, Replace All...), choosing one undoes it and those after it [WP12]
- ✅ Redo (Toolbar) `edit.redo` — `ID_REDO`
- ✅ Repeat (Toolbar) `edit.repeat` — F4 (`ID_FX_REPEAT`): the last command that made an undo step again (main.c `wp12_note`/`wp12_replay`, lists with their entry), or the text typed since it (`doc_undo_typed()`) [WP12]
- ✅ AutoSave (Toolbar) `file.autosave` — `ID_INFO_AUTOSAVE`: switch on the quick access bar, File › Info › AutoSave and File › Options; saves a document with a .docx of its own 3 s after typing stops (at most every 30 s) [WP24]

### Title Bar (1: ✅ 0 · 🟨 0 · ⬜ 1 · ➖ 0)

- ⬜ Search (Tell Me) (Search) `tools.search` — no command search [WP26]

### Home (105: ✅ 96 · 🟨 2 · ⬜ 6 · ➖ 1)

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
- 🟨 Text Effects and Typography (Font) `format.textEffects` — the Font group's Text Effects list (`ID_C_EFFECTS`): outline, shadow, emboss, engrave, hidden, shading, underline colour, enclose, standard ligatures and old-style/lining numbers (kept as `w14:ligatures`/`w14:numForm`, not drawn: the Amiga's fonts have no OpenType features). Every other `w14` text effect (fill, outline colour, glow, reflection, 3-D) and `w:effect`/`w:kern` are kept verbatim through a save (`CharFmt.rx`, core/charfx.c); their galleries are WordArt's, WP19 [WP9]
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
- ⬜ Outline Level (Paragraph) `para.outlineLevel` — `ParaFmt` has no outline level (only "heading N" style names count, core/toc.c) [WP11]
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
- ✅ Modify Style (Styles) `styles.modify` — `ID_MODSTYLE` (`open_style()`; no tabs/borders/numbering in it)
- ✅ Update Style to Match (Styles) `styles.updateToMatch` — `ID_STYLEUPDATE`
- ✅ Delete Style (Styles) `styles.delete` — `ID_STYLEDELETE`
- 🟨 Styles Pane (Styles) `styles.pane` — `ID_STYLESPANE` opens a modal-less Styles window (Manage), not a docked pane with previews/options [WP13]
- ⬜ Style Inspector (Styles) `styles.inspector` [WP13]
- ✅ Manage Styles (Styles) `styles.manage` — `ID_STYLESPANE` (apply, modify, delete, new)
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

### Insert (53: ✅ 32 · 🟨 1 · ⬜ 14 · ➖ 6)

- ✅ Cover Page (Pages) `insert.coverPage` — `ID_COVERDD` (3 designs, remove)
- ✅ Blank Page (Pages) `insert.blankPage` — `ID_BLANKPAGE`
- ✅ Page Break (Pages) `insert.pageBreak` — `ID_PAGEBREAK`
- ✅ Insert Table (Tables) `insert.table` — `ID_TABLE` (`open_table()`)
- ⬜ Draw Table (Tables) `table.draw` — README 0.4 "Not yet: drawing tables with the mouse" [WP21b]
- ✅ Convert Text to Table (Tables) `table.fromText` — `ID_TOTABLE`
- ⬜ Excel Spreadsheet (Tables) `insert.spreadsheet` — WordCraft's makes a table from CSV text [WP21a]
- ⬜ Quick Tables (Tables) `table.quick` [WP21a]
- ✅ Pictures (Illustrations) `insert.picture` — `ID_PICTURE` (PNG, JPEG only)
- ➖ Online Pictures (Illustrations) `insert.onlinePicture` — WordCraft doesn't have it either (online service)
- ✅ Shapes (Illustrations) `insert.shape` — `ID_SHAPESDD` (24 presets, `SH_` in doc.h)
- ✅ Icons (Illustrations) `insert.icon` — `ID_SH_ICONS` (amiga/wp_shapes.c): a window of 52 icons in six categories, drawn in Quill's own icon language (core/shapex.c), inserted as a shape in its own colour; saved as `a:custGeom` named "Icon: …" in `wp:docPr`, which LibreOffice and Word draw and Quill reads back as the icon [WP18]
- ➖ 3D Models (Illustrations) `insert.model3d` — WordCraft doesn't have it either
- ⬜ SmartArt (Illustrations) `insert.smartArt` [WP15c]
- ⬜ Chart (Illustrations) `insert.chart` [WP15b]
- ➖ Screenshot (Illustrations) `insert.screenshot` — WordCraft doesn't have it either
- ⬜ Drawing Canvas (Illustrations) `insert.canvas` — README roadmap [WP20]
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
- ⬜ Quick Parts (Text) `insert.quickParts` — no building blocks / glossary [WP23]
- ⬜ AutoText (Text) `insert.autoText` [WP23]
- ⬜ Document Property (Text) `insert.docProperty` [WP1]
- ⬜ Field (Text) `insert.field` — no Insert Field: only PAGE/NUMPAGES/PAGEREF/REF/HYPERLINK/TOC are understood (docx.c `field_kind()`) [WP1]
- ⬜ WordArt (Text) `insert.wordArt` [WP19]
- ✅ Drop Cap (Text) `insert.dropCap` — `ID_DROPCAPDD`, `ID_DROPCAPDLG`
- ⬜ Signature Line (Text) `insert.signatureLine` [WP19]
- 🟨 Date & Time (Text) `insert.dateTime` — `ID_DATE` types today's date as text in one format (`today_text()`); no format choice, no DATE field that updates [WP1]
- ⬜ Object (Text) `insert.object` — WordCraft's inserts a file's text [WP23]
- ⬜ Text from File (Text) `insert.textFromFile` [WP23]
- ✅ Equation (Symbols) `insert.equation` — Insert › Equation (Insert menu, and the Equation button of Insert › Symbols, amiga/wp_math.c): the equation typed in linear format (UnicodeMath subset: a/b, x^2, x_i, √(…), ∑_(i=1)^n▒…, brackets, functions, accents, ■ matrices, █ arrays, □ ▭ ⟡ boxes, "text"; characters outside Latin-1 as \control words), a display equation when the paragraph is empty as in Word; with an equation selected (or "Edit Equation..." on the right-button menu) it is changed. Checked: tests/test_wp14a.c (linear format in and out), emulator screenshot of the dialog [WP14a]
- ➖ Ink Equation (Symbols) `insert.inkEquation` — WordCraft doesn't have it either
- ✅ Symbol (Symbols) `insert.symbol` — the Symbol list's "More Symbols..." and WP10's Symbol window (`ID_P_SYMBOL`, Insert menu): every Latin-1 character as a button (what the interface can show), any Unicode character by its code (hex or decimal), inserted in a font chosen, the recently used ones, and a Special Characters page (dashes, spaces, hyphens, quotes, ©, ®, ™...) [WP10]
- ✅ Nonbreaking Space (Symbols) `text.nbsp` — `ID_P_NBSP` (Ctrl-Shift-Space, Insert › Special Characters, the Symbol window), shown as ° with formatting marks [WP10]
- ✅ Nonbreaking Hyphen (Symbols) `text.nbHyphen` — U+2011 (`ID_P_NBHYPHEN`, Ctrl-Shift-minus): drawn and measured as a hyphen, never broken at (lines now break after ordinary hyphens), `w:noBreakHyphen` read and written [WP10]
- ✅ Optional Hyphen (Symbols) `text.optionalHyphen` — U+00AD (`ID_P_SOFTHYPHEN`, Ctrl-minus): no room and not drawn unless a line breaks there (then a hyphen), ¬ with formatting marks, `w:softHyphen` read and written [WP10]

### Draw (11: ✅ 0 · 🟨 0 · ⬜ 10 · ➖ 1)

- ⬜ Select (Tools) `draw.select` — no ink tools (objects are selected by clicking, but there is no Draw tab) [WP20]
- ⬜ Lasso Select (Tools) `draw.lasso` [WP20]
- ⬜ Eraser (Tools) `draw.eraser` [WP20]
- ⬜ Pen (Pens) `draw.pen` [WP20]
- ⬜ Pencil (Pens) `draw.pencil` [WP20]
- ⬜ Highlighter (Pens) `draw.highlighter` [WP20]
- ⬜ Add Pen (Pens) `draw.addPen` [WP20]
- ⬜ Ink to Shape (Convert) `draw.inkToShape` [WP20]
- ➖ Ink to Math (Convert) `draw.inkToMath` — WordCraft doesn't have it either
- ⬜ Drawing Canvas (Insert) `insert.canvas` [WP20]
- ⬜ Ink Replay (Replay) `draw.replay` [WP20]

### Design (10: ✅ 9 · 🟨 0 · ⬜ 1 · ➖ 0)

- ✅ Themes (Document Formatting) `design.theme` — `ID_THEMEDD` (8 Quill themes setting style set, fonts and colours; not Office theme parts)
- ✅ Style Set (Document Formatting) `design.styleSet` — `ID_STYLESETDD` (6)
- ✅ Colors (Document Formatting) `design.themeColors` — `ID_COLORSETDD` (8)
- ✅ Fonts (Document Formatting) `design.themeFonts` — `ID_THEMEFONTDD` (6 pairs)
- ✅ Paragraph Spacing (Document Formatting) `design.paragraphSpacing` — `ID_PSPACINGDD` (6)
- ⬜ Effects (Document Formatting) `design.effects` [WP13]
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

### References (25: ✅ 8 · 🟨 2 · ⬜ 14 · ➖ 1)

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
- 🟨 Update Fields (Fields) `references.updateFields` — page fields, PAGEREF cross-references and page numbers update themselves; the TOC through `ID_TOCUPDATE`; no general Update Fields (F9) [WP1]

### Mailings (23: ✅ 0 · 🟨 0 · ⬜ 22 · ➖ 1)

- ⬜ Envelopes (Create) `mailings.envelopes` — no Mailings tab at all [WP4]
- ⬜ Labels (Create) `mailings.labels` [WP4]
- ⬜ Start Mail Merge (Start Mail Merge) `mailings.start` [WP4]
- ⬜ Select Recipients (Start Mail Merge) `mailings.recipients` [WP4]
- ⬜ Edit Recipient List (Start Mail Merge) `mailings.editRecipients` [WP4]
- ⬜ Filter Recipients (Start Mail Merge) `mailings.filterRecipients` [WP4]
- ⬜ Sort Recipients (Start Mail Merge) `mailings.sortRecipients` [WP4]
- ⬜ Highlight Merge Fields (Write & Insert Fields) `mailings.highlightFields` [WP4]
- ⬜ Address Block (Write & Insert Fields) `mailings.addressBlock` [WP4]
- ⬜ Greeting Line (Write & Insert Fields) `mailings.greetingLine` [WP4]
- ⬜ Insert Merge Field (Write & Insert Fields) `mailings.insertField` [WP4]
- ⬜ Rules (Write & Insert Fields) `mailings.rules` [WP4]
- ⬜ Match Fields (Write & Insert Fields) `mailings.matchFields` [WP4]
- ⬜ Preview Results (Preview Results) `mailings.preview` [WP4]
- ⬜ Next Record (Preview Results) `mailings.next` [WP4]
- ⬜ Previous Record (Preview Results) `mailings.previous` [WP4]
- ⬜ First Record (Preview Results) `mailings.first` [WP4]
- ⬜ Last Record (Preview Results) `mailings.last` [WP4]
- ⬜ Go to Record (Preview Results) `mailings.goToRecord` [WP4]
- ⬜ Find Recipient (Preview Results) `mailings.findRecipient` [WP4]
- ⬜ Check for Errors (Preview Results) `mailings.checkErrors` [WP4]
- ⬜ Finish & Merge (Finish) `mailings.finish` [WP4]
- ➖ Print Documents (Finish) `mailings.printDocuments` — printing left out on purpose (merging to a new document or PDF is part of Finish & Merge)

### Review (31: ✅ 21 · 🟨 1 · ⬜ 7 · ➖ 2)

- 🟨 Spelling & Grammar (Proofing) `review.spelling` — `ID_SPELL` spelling only (British English, core/spell.c); no grammar [WP8]
- ⬜ Thesaurus (Proofing) `review.thesaurus` [WP8]
- ✅ Word Count (Proofing) `review.wordCount` — `ID_WORDCOUNT` (`word_count()`)
- ⬜ Read Aloud (Speech) `review.readAloud` — (narrator.device/translator.library could speak it offline) [WP8]
- ✅ Check Accessibility (Accessibility) `file.accessibility` — Review › Accessibility, `ID_INFO_ACC` [WP24]
- ✅ Alt Text (Accessibility) `view.altTextPane` — `ID_INFO_ALTPANE` / `ID_ALTTEXT`: Alt Text pane following the selected object, Mark as decorative (`adec:decorative` read and written); table alt text is WP21b's (`table.altText`) [WP24]
- ➖ Translate (Language) `review.translate` — needs an online service; WordCraft doesn't have it either
- ⬜ Language (Language) `review.language` — no proofing language per text; one British English dictionary [WP31]
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
- ⬜ Hide Ink (Ink) `review.hideInk` — (no ink) [WP20]

### View (29: ✅ 18 · 🟨 2 · ⬜ 4 · ➖ 5)

- ✅ Read Mode (Views) `view.readMode` — `ID_VREAD`
- ✅ Print Layout (Views) `view.printLayout` — `ID_VPRINT`
- ✅ Web Layout (Views) `view.webLayout` — `ID_VWEB`
- ⬜ Outline (Views) `view.outline` — no Outline view (`VM_` has none) [WP11]
- ✅ Draft (Views) `view.draft` — `ID_VDRAFT`
- ✅ Focus (Immersive) `view.focus` — `ID_FOCUS` (status bar)
- ✅ Immersive Reader (Immersive) `view.immersive` — WordCraft's command toggles Read Mode: Quill `ID_VREAD` (no line focus, syllables, text spacing)
- ✅ Vertical (Page Movement) `view.vertical` — pages run down the window (the only page movement)
- ⬜ Side to Side (Page Movement) `view.sideToSide` [WP25]
- ✅ Ruler (Show) `view.ruler` — `ID_RULER`
- ✅ Gridlines (Show) `view.gridlines` — `ID_GRID`
- ✅ Navigation Pane (Show) `view.navigationPane` — `ID_OUTLINE` (headings, pages, results; panes.c)
- ✅ Zoom (Zoom) `view.zoom` — `ID_ZOOMDD` (list); no Zoom dialog
- ✅ 100% (Zoom) `view.zoom100` — `ID_ZOOM100`, `ID_ZOOM100B`
- ✅ One Page (Zoom) `view.onePage` — `ID_ONEPAGE`
- 🟨 Multiple Pages (Zoom) `view.multiplePages` — `ID_MULTIPAGE` two pages a row (more when wide); no choice of how many [WP25]
- ✅ Page Width (Zoom) `view.pageWidth` — `ID_PAGEWIDTH`
- ✅ Zoom In (Zoom) `view.zoomIn` — status bar + (`ID_ZOOMSET`, panes.c)
- ✅ Zoom Out (Zoom) `view.zoomOut` — status bar - (`ID_ZOOMSET`)
- ✅ Switch Modes (Dark Mode) `view.darkMode` — `ID_DARKPAGE`
- ⬜ New Window (Window) `view.newWindow` — a second window on the same document isn't possible (each window holds its own document) [WP25]
- ➖ Arrange All (Window) `view.arrangeAll` — WordCraft doesn't have it either
- ⬜ Split (Window) `view.split` [WP25]
- ➖ View Side by Side (Window) `view.sideBySide` — WordCraft doesn't have it either
- ➖ Synchronous Scrolling (Window) `view.syncScroll` — WordCraft doesn't have it either
- 🟨 Switch Windows (Window) `view.switchWindows` — `ID_NEXTWIN` (Amiga-`) cycles; no list of open documents (README roadmap "a Window menu") [WP25]
- ➖ Macros (Macros) `tools.macros` — macros and scripting left out on purpose
- ➖ Record Macro (Macros) `tools.recordMacro` — macros and scripting left out on purpose
- ✅ Properties (SharePoint) `file.properties` — as File › Properties: `ID_INFO_PROPS` [WP24]

### Read Mode (3: ✅ 0 · 🟨 0 · ⬜ 2 · ➖ 1)

- ⬜ Column Width (View) `view.readColumnWidth` — Read Mode always shows two pages a row [WP25]
- ⬜ Page Color (View) `view.readPageColor` — no sepia/inverse in Read Mode (Dark Mode is separate) [WP25]
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
- ➖ Unlink Citations (Document) `ui.zotero.removeCodes` — (as above; keeping Zotero's ADDIN fields through a save is in WP1)

### Help (4: ✅ 1 · 🟨 0 · ⬜ 0 · ➖ 3)

- ✅ Help (Help) `ui.dialog about` — `ID_ABOUT` (About, File page `BP_ABOUT`); bubble help on every tool
- ➖ Check for Updates (Help) `ui.checkForUpdates` — network
- ➖ Community Discord (Help) `ui.discord` — network (opens a web page)
- ➖ Commands (Agents) `ui.dialog commands` — WordCraft's list of command ids for agents/scripts: control protocols left out on purpose

### Table Design (8: ✅ 4 · 🟨 0 · ⬜ 4 · ➖ 0)

- ✅ Table Style Options (Table Style Options) `table.look` — `ID_LK_FIRSTROW` … `ID_LK_BANDCOLS` (6)
- ✅ Table Styles (Table Styles) `table.style` — `ID_TSTYLEDD`
- ⬜ New Table Style (Table Styles) `table.newStyle` — README roadmap "a table style editor" [WP21a]
- ⬜ Modify Table Style (Table Styles) `table.modifyStyle` [WP21a]
- ⬜ Delete Table Style (Table Styles) `table.deleteStyle` [WP21a]
- ✅ Shading (Table Styles) `table.shading` — `ID_SHADINGDD`
- ✅ Borders (Borders) `table.borders` — `ID_TBORDERSDD` with pen `ID_PENSTYLE`/`ID_PENWIDTH`/`ID_PENCOLOR`
- ⬜ Border Painter (Borders) `table.borderPainter` [WP21a]

### Table Layout (33: ✅ 26 · 🟨 1 · ⬜ 6 · ➖ 0)

- ✅ Select Table (Table) `table.selectTable` — `ID_TSELECTDD` (`CMD_TSELECT` 3)
- ✅ Select Row (Table) `table.selectRow` — `ID_TSELECTDD`
- ✅ Select Column (Table) `table.selectColumn` — `ID_TSELECTDD`
- ✅ Select Cell (Table) `table.selectCell` — `ID_TSELECTDD`
- ✅ View Gridlines (Table) `table.viewGridlines` — `ID_TGRID`
- ✅ Properties (Table) `table.properties` — `ID_TPROPS` (`open_tprops()`)
- ⬜ Alt Text (Table) `table.altText` — no w:tblCaption/w:tblDescription [WP21b]
- ⬜ Table Direction (Table) `table.rtl` [WP27]
- ⬜ Draw Table (Draw) `table.draw` [WP21b]
- ⬜ Eraser (Draw) `table.eraser` [WP21b]
- ⬜ Delete Cells (Rows & Columns) `table.deleteCells` — rows, columns or table only (`ID_DELTABLEDD`); no Delete Cells (shift left/up) [WP21b]
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
- 🟨 Alignment (Alignment) `table.cellAlign` — `ID_VALIGN0`-`2`: top, centre, bottom only; WordCraft's 9-way grid sets the paragraphs' alignment too [WP21b]
- ⬜ Text Direction (Alignment) `table.textDirection` — README roadmap "text direction in cells" [WP21b]
- ✅ Cell Margins (Alignment) `table.cellMargins` — `ID_TPROPS` (table cell margins; no per-cell margins)
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

### Shape Format (13: ✅ 9 · 🟨 0 · ⬜ 4 · ➖ 0)

- ✅ Alt Text (Accessibility) `object.altText` — `ID_ALTTEXT` (Shape Format's Accessibility group, `o_salt`)
- ✅ Shape Fill (Shape Styles) `shape.fill` — `ID_OBJFILLDD` (solid colours)
- ✅ Shape Fill Gradient (Shape Styles) `shape.fill` — `ID_SH_GRADDD` (light and dark variations, linear, radial, from the corners), the pane's gradient settings; picture fills `ID_SH_PICFILL`; `a:gradFill`, a shape's `a:blipFill` [WP18]
- ✅ Format Shape Pane (Shape Styles) `view.formatShapePane` — `ID_SH_PANE`: a pane window beside the page that follows the selection and changes the object as you set it (fill, line, shadow, glow, soft edges, size, rotation, text box alignment, direction and margins; for a picture its corrections, colour, transparency and artistic effect) [WP18]
- ✅ Shape Outline (Shape Styles) `shape.outline` — `ID_OBJLINEDD`
- ✅ Shape Effects (Shape Styles) `shape.effects` — `ID_SH_EFFECTSDD`: shadows, glows, soft edges (`a:outerShdw`, `a:glow`, `a:softEdge`), drawn on the screen and in PDFs [WP18]
- ⬜ WordArt Quick Styles (WordArt Styles) `wordArt.style` [WP19]
- ⬜ Text Fill (WordArt Styles) `wordArt.textFill` [WP19]
- ⬜ Text Outline (WordArt Styles) `wordArt.textOutline` [WP19]
- ⬜ Text Effects Transform (WordArt Styles) `wordArt.transform` [WP19]
- ✅ Text Direction (Text) `shape.textDirection` — `ID_SH_TEXTDIRDD` (`bodyPr vert`: across, turned 90° or 270°, drawn turned on the screen and in PDFs) [WP18]
- ✅ Align Text (Text) `shape.alignText` — `ID_SH_ALIGNDD` (`bodyPr anchor`: top, middle, bottom; core/objtext.c) [WP18]
- ✅ Create Link (Text) `shape.link` — `ID_SH_LINK` (then click the empty box) and Break Link `ID_SH_UNLINK`; the chain's text flows from box to box (core/objtext.c), `wps:txbx id`/`wps:linkedTxbx` read and written [WP18]

### Header & Footer (5: ✅ 5 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Go to Header (Navigation) `insert.editHeader` — `ID_HFHEADER`
- ✅ Go to Footer (Navigation) `insert.editFooter` — `ID_HFFOOTER`
- ✅ Previous Section (Navigation) `hf.previous` — `ID_HFPREV` (`headfoot_section(-1)`)
- ✅ Next Section (Navigation) `hf.next` — `ID_HFNEXT`
- ✅ Header from Top (Position) `hf.position` — `ID_PAGESETUPDLG` on the Header & Footer tab (header and footer distances)

### Equation (22: ✅ 1 · 🟨 0 · ⬜ 20 · ➖ 1)

- ✅ Equation (Tools) `insert.equation` — equations read, laid out, drawn and written (see 2.2); Insert › Equation (Insert menu, and the Equation button of Insert › Symbols, amiga/wp_math.c): the equation typed in linear format (UnicodeMath subset: a/b, x^2, x_i, √(…), ∑_(i=1)^n▒…, brackets, functions, accents, ■ matrices, █ arrays, □ ▭ ⟡ boxes, "text"; characters outside Latin-1 as \control words), a display equation when the paragraph is empty as in Word; with an equation selected (or "Edit Equation..." on the right-button menu) it is changed. Checked: tests/test_wp14a.c (linear format in and out), emulator screenshot of the dialog [WP14a]
- ➖ Ink Equation (Tools) `insert.inkEquation` — WordCraft doesn't have it either
- ⬜ Unicode (Conversions) `equation.inputFormat` [WP14b]
- ⬜ LaTeX (Conversions) `equation.inputFormat` [WP14b]
- ⬜ Convert (Conversions) `equation.convert` [WP14b]
- ⬜ Normal Text (Conversions) `equation.normalText` [WP14b]
- ⬜ Symbols (Symbols) `equation.insertSymbol` [WP14b]
- ⬜ Fraction (Structures) `equation.insertStructure` [WP14b]
- ⬜ Script (Structures) `equation.insertStructure` [WP14b]
- ⬜ Radical (Structures) `equation.insertStructure` [WP14b]
- ⬜ Integral (Structures) `equation.insertStructure` [WP14b]
- ⬜ Large Operator (Structures) `equation.insertStructure` [WP14b]
- ⬜ Bracket (Structures) `equation.insertStructure` [WP14b]
- ⬜ Function (Structures) `equation.insertStructure` [WP14b]
- ⬜ Accent (Structures) `equation.insertStructure` [WP14b]
- ⬜ Limit and Log (Structures) `equation.insertStructure` [WP14b]
- ⬜ Operator (Structures) `equation.insertStructure` [WP14b]
- ⬜ Matrix (Structures) `equation.insertStructure` [WP14b]
- ⬜ Change to Inline / Display (Equation Options) `equation.display` [WP14b]
- ⬜ Justification (Equation Options) `equation.justify` [WP14b]
- ⬜ Equation Number (#) (Equation Options) `equation.number` [WP14b]
- ⬜ Structure Commands (Equation Options) `equation.structure` [WP14b]

### Outlining (13: ✅ 0 · 🟨 0 · ⬜ 13 · ➖ 0)

- ⬜ Promote to Heading 1 (Outline Tools) `outline.promoteToHeading1` — no Outline view [WP11]
- ⬜ Promote (Outline Tools) `outline.promote` [WP11]
- ⬜ Outline Level (Outline Tools) `outline.level` [WP11]
- ⬜ Demote (Outline Tools) `outline.demote` [WP11]
- ⬜ Demote to Body Text (Outline Tools) `outline.demoteToBody` [WP11]
- ⬜ Move Up (Outline Tools) `outline.moveUp` [WP11]
- ⬜ Move Down (Outline Tools) `outline.moveDown` [WP11]
- ⬜ Expand (Outline Tools) `outline.expand` [WP11]
- ⬜ Collapse (Outline Tools) `outline.collapse` [WP11]
- ⬜ Show Level (Outline Tools) `outline.showLevel` [WP11]
- ⬜ Show Text Formatting (Outline Tools) `outline.showFormatting` [WP11]
- ⬜ Show First Line Only (Outline Tools) `outline.firstLineOnly` [WP11]
- ⬜ Close Outline View (Close) `outline.close` [WP11]

### SmartArt Design (7: ✅ 0 · 🟨 0 · ⬜ 7 · ➖ 0)

- ⬜ Add Shape (Create Graphic) `smartArt.addShape` — SmartArt is read, drawn from its saved drawing and kept through a save (WP15a), but not edited [WP15c]
- ⬜ Promote (Create Graphic) `smartArt.promote` [WP15c]
- ⬜ Demote (Create Graphic) `smartArt.demote` [WP15c]
- ⬜ Text Pane (Create Graphic) `smartArt.textPane` [WP15c]
- ⬜ Layouts (Layouts) `smartArt.layout` [WP15c]
- ⬜ Change Colors (SmartArt Styles) `smartArt.colors` [WP15c]
- ⬜ Reset Graphic (Reset) `smartArt.reset` [WP15c]

### Chart Design (5: ✅ 0 · 🟨 0 · ⬜ 5 · ➖ 0)

- ⬜ Change Chart Type (Type) `chart.type` — charts are read, drawn from their cached values and kept through a save (WP15a), but not edited [WP15b]
- ⬜ Edit Data (Data) `chart.editData` [WP15b]
- ⬜ Chart Title (Chart Layouts) `chart.title` [WP15b]
- ⬜ Legend (Chart Layouts) `chart.legend` [WP15b]
- ⬜ Data Labels (Chart Layouts) `chart.dataLabels` [WP15b]

### Editing (41: ✅ 29 · 🟨 2 · ⬜ 10 · ➖ 0)

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
- ⬜ Move Paragraph Up (Keyboard) `para.moveUp` [WP11]
- ⬜ Move Paragraph Down (Keyboard) `para.moveDown` [WP11]
- 🟨 Promote (Keyboard) `para.promote` — Shift-Tab raises a list item's level; headings can't be promoted [WP11]
- 🟨 Demote (Keyboard) `para.demote` — Tab lowers a list item's level; headings can't be demoted [WP11]
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
- ⬜ Show Field Codes (Fields) `view.fieldCodes` — field codes are not kept [WP1]
- ⬜ Toggle Field Codes (Fields) `fields.toggleCode` [WP1]
- ⬜ Edit Field Code (Fields) `fields.setCode` [WP1]
- ⬜ Unlink Fields (Fields) `fields.unlink` [WP1]
- ⬜ Lock Fields (Fields) `fields.lock` [WP1]
- ⬜ Unlock Fields (Fields) `fields.unlock` [WP1]
- ⬜ Next Field (Fields) `fields.next` [WP1]
- ⬜ Previous Field (Fields) `fields.previous` [WP1]

### Tools (7: ✅ 1 · 🟨 0 · ⬜ 6 · ➖ 0)

- ⬜ AutoCorrect Options (Proofing) `tools.autocorrect` — no AutoCorrect [WP8]
- ⬜ Set Proofing Language (Proofing) `review.language` [WP31]
- ⬜ Customize Ribbon (Customize) `tools.customizeRibbon` [WP26]
- ⬜ Customize Keyboard (Customize) `tools.customizeKeyboard` [WP26]
- ⬜ Encrypt with Password (Protection) `file.encrypt` [WP7]
- ⬜ Templates and Add-ins (Templates) `tools.templates` — no attached templates [WP23]
- ✅ Full Screen View (Views) `view.fullScreenView` — `ID_FOCUS` hides the ribbon, rulers, pane and status bar (`chrome()`)

## 2. Beyond the ribbon

Same marks as above. WordCraft's side comes from its docs (file-format-parity.md, layout-parity.md, typing-parity.md, localization-parity.md, target-app-parity.md) and its source (crates/docx, crates/formats, crates/docbin, crates/layout, crates/pdf, crates/proof, crates/zotero, crates/mcp, crates/ui-egui/src/options.rs, crates/engine/src/keymap.rs). Quill's side was checked in core/docx.c, core/layout.c, core/pdf.c, core/doc.h, amiga/main.c, amiga/view.c, amiga/panes.c and amiga/backstage.c.

### 2.1 File formats

- ✅ **.docx read and write** — core/docx.c (`docx_read`/`docx_write`), the only format Quill saves with formatting.
- 🟨 **.docx Strict Open XML (read)** — untested in Quill: it finds the main part by the relationship's name ending (`rel_target(..., "/officeDocument")`, docx.c) and matches elements by local name, so a Strict file may open, but Strict's unit-suffixed measures (`13.90pt`, `50%`) aren't parsed. WordCraft maps the Strict namespaces and units (`xml.rs`, #2329).
- 🟨 **.dotx / .docm / .dotm** — Quill reads any package with word/document.xml whatever its name, but never writes a template, and drops macros (WordCraft keeps vbaProject on save without running it).
- ⬜ **.doc Word 97-2003 (read)** — WordCraft has a spec-based reader (crates/docbin: text, formatting, styles, sections, tables, lists, notes, fields, pictures, shapes, RC4/XOR encryption). Quill: none.
- ✅ **.rtf read and write** [WP28a] — core/rtf.c (row `fmt_rtf`, sniffed by `{\rtf`; Open/Save As take .rtf, File > Save As Rich Text... saves a copy): text, character and paragraph formatting, styles (\sbasedon, \snext; a file's unused styles aren't made), lists (\listtable/\ls and Word 6's \pn), tables with merged cells (by width, \clmgf/\clmrg, \clvmgf/\clvmrg), borders, shading, row heights, heading rows; PNG/JPEG pictures (hex or \bin, \shppict, shapes' pib), WMF/EMF/bitmaps as a dashed box of their size; headers/footers (default, first, even), footnotes, endnotes, HYPERLINK/PAGE/NUMPAGES/PAGEREF fields (others keep their result), bookmarks, tracked changes (\revtbl, \revised, \deleted), sections with page setup, columns, line numbers, vertical alignment, \info title/author; code pages 437/850/852/866/874/1250-1258/10000 (core/codepage.c), \uN with \uc fallbacks and surrogates. Evidence: tests/test_wp28a.c (a document with all of these round-trips with same_doc and writes the same file twice; Word's and LibreOffice's 7 test .docx files through RTF; RTF that LibreOffice wrote (tests/data/lo-*.rtf) reads as the .docx does); tools/lo_formats.sh: LibreOffice converts what Quill writes to .docx with every paragraph's text, notes, tables and pictures, and LibreOffice renders it as it renders the original .docx (checked by eye with pdftoppm). 🟨 left out: nested tables are flattened into their cell (read and write, LOST_TABLES), shapes/text boxes and comments aren't written, floating pictures are written in line, picture cropping, double-byte code pages without \u (932/936/949/950 text shows as '?').
- ⬜ **.odt read and write** — WordCraft crates/formats/src/odt.
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
- ⬜ **Spreadsheets (.xlsx, .xls, .ods, .csv) as mail-merge recipient lists** — WordCraft crates/formats/src/sheet. Quill has no mail merge.
- 🟨 **Picture formats for Insert › Picture** — Quill: PNG and JPEG (baseline, progressive, CMYK; core/image.c); WP17: GIF, BMP and IFF ILBM (EHB, HAM6/8, deep, PBM; readers from Easel in core/picfmt.c), GIF and BMP saved as they are, ILBM as PNG. Not yet: WebP, WMF and EMF (WordCraft has them).
- ➖ **.json document model** — WordCraft's format for agents (control protocol, left out on purpose).

### 2.2 DOCX features

- 🟨 **Run and paragraph properties, styles, numbering, sections** — Quill reads and writes the common set (core/docx.c). Missing: complex-script properties (`w:rtl`, `w:cs`, `w:szCs`, `w:bCs`) and East Asian font slots (`w:eastAsia`), languages (`w:lang`, none read), every `ST_NumberFormat` beyond decimal/letters/roman (`NF_` in doc.h), indents in characters, more than 8 tab stops a paragraph (`ParaFmt.tabs[8]`), hidden, emboss, imprint, outline, shadow, underline colour, `w:outlineLvl`.
- 🟨 **Positional tabs (`w:ptab`)** — read as an ordinary tab (docx.c line 1355), not placed by alignment/margin.
- 🟨 **Tables** — widths, merges, borders, shading, styles with conditional formatting, nested tables (one level), cantSplit, heading rows: ✅. Floating tables (`w:tblpPr`) are placed in the text (README roadmap); row exceptions (`w:tblPrEx`) and right-to-left tables (`w:bidiVisual`) not read; tables in headers and footers come in as their text (README roadmap).
- ✅ **Headers/footers, page borders, line numbers, gutter, mirror margins** — first/even/odd, per section, link to previous: ✅. WP22a: `w:pgMar w:gutter`, `w:rtlGutter`, `w:gutterAtTop`, `w:mirrorMargins`, `w:printTwoOnOne`, `w:bookFoldPrinting`/`w:bookFoldRevPrinting`/`w:bookFoldPrintingSheets`, `w:bordersDoNotSurroundHeader/Footer`; page borders per section and per side (`w:pgBorders` every attribute), read and written (tests/test_wp22a.c).
- ✅ **Hyphenation settings** — `w:autoHyphenation`, `w:hyphenationZone`, `w:doNotHyphenateCaps`, `w:consecutiveHyphenLimit` read, written and laid out (WP22a: core/pages.c `pages_hyph_zone`, `pages_hyph_allowed`).
- ✅ **Footnotes, endnotes, comments** — read and written. Note options (`w:footnotePr`/`w:endnotePr`) and the document's own separators and continuation notice read and written (WP22b ✅). Comments' replies, resolved state, durable ids and UTC times, and the people (`commentsExtended`, `commentsIds`, `commentsExtensible`, `people.xml`, each person's own entry kept) ✅ (WP5, core/docx.c `wp5_read_parts()`, `wp5_write_parts()`).
- ✅ **Tracked insertions and deletions** — `w:ins`/`w:del` with author and date (docx.c, `REV_INS`/`REV_DEL`).
- 🟨 **Formatting revisions (`w:rPrChange`, `w:pPrChange`, `w:sectPrChange`, table ones, `w:numberingChange`)** — WP5: a run's and a paragraph's (`CharFmt.frev`, `ParaFmt.prev`, core/review.c) read with the old formatting (and its XML kept verbatim), shown (balloons, bars, Original), accepted and rejected, recorded while tracking (every formatting command) and written; `w:tblPrChange`, `w:tblGridChange`, `w:trPrChange`, `w:tcPrChange`, inserted and deleted rows and cells, `w:sectPrChange` kept verbatim with their table, row, cell or section (accepted by dropping them; rejecting them is refused). Missing: the legacy `w:numberingChange` and revisions inside style definitions (styles.xml) are dropped; a paragraph mark's own insertion, deletion and formatting change are not kept.
- ✅ **Move tracking (`w:moveFrom`/`w:moveTo`)** — WP5: read as a deletion and an insertion that are one move (`CharFmt.move`, by the name of its `w:moveFromRangeStart`/`w:moveToRangeStart`), drawn in the move colour and marks, written back as `w:moveFrom`/`w:moveTo` inside named ranges (LibreOffice reads them as a move). Quill's own cut and paste while tracking is recorded as a deletion and an insertion, not a move.
- 🟨 **Fields** — understood: PAGE, NUMPAGES/SECTIONPAGES, REF, PAGEREF, HYPERLINK, the TOC (core/docx.c `field_kind()`, `ref_field()`, `hyperlink_field()`). Every other field keeps only its result as plain text: the code is dropped, so DATE, SEQ, MERGEFIELD, ADDIN (Zotero/Mendeley citations), IF, DOCPROPERTY, INDEX, XE, TC… don't survive a save as fields. WordCraft keeps every code and its cached result and evaluates most of them.
- ⬜ **Content controls (`w:sdt`)** — Quill reads their content and drops the controls (docx.c treats `sdt`/`sdtContent` as transparent); WordCraft keeps and edits every kind (rich/plain text, checkbox, combo, drop-down, date, picture, building block, repeating section, data binding kept verbatim).
- ⬜ **Restrict Editing (`w:documentProtection`, `w:permStart`/`w:permEnd`, locked styles)** — not read or kept.
- ⬜ **Legacy form fields (`w:ffData`: FORMTEXT, FORMCHECKBOX, FORMDROPDOWN)** — WordCraft keeps and draws them; Quill keeps only the result text.
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
- ✅ **Equations (OMML `m:oMath`)** — WP14a: every ECMA-376 §22.1 element is read into a tree (core/math.c: `m:oMath`, `m:oMathPara` with its justification, fractions bar/skewed/linear/no bar, sub/superscripts and pre-scripts, radicals, n-ary operators with limits, delimiters with separators, functions, accents, bars, group characters, lower and upper limits, matrices with column specs, equation arrays, boxes, border boxes, phantoms, argument sizes, and every property, each kept as absent/on/off), with `m:ctrlPr`, run `w:rPr`, unknown properties and elements (`w:ins` round math...) kept verbatim; written back the same (its m namespace declared on each equation); settings.xml `m:mathPr` kept and used (limits, default justification, small fractions, wrap indent). Laid out TeX-style with Quill's own fonts (core/mathlay.c; math italic, display and text styles, script sizes, TeX spacing) and drawn on screen and in PDFs. Checked: a Word-style sample written from the spec (tests/data/equations.docx, every construct) reads, round trips to identical OMML and a stable document.xml, LibreOffice renders Quill's saved copy exactly as the original, and documents with equations made in Quill open in LibreOffice with them. Left: a display equation too wide for its line isn't broken automatically (only at `m:brk`); block-level `m:oMathPara` outside a paragraph is skipped.
- 🟨 **Themes, font table, settings, compatibility mode** — theme fonts are read (major/minor), compatibility mode 15 is written; the theme part's colours (`a:clrScheme`) only resolve shape colours, and the document's theme isn't written back as its own; embedded fonts (`w:embedRegular`…) not read.
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
- 🟨 **Preserving what isn't modelled** — WordCraft keeps unknown parts, settings and custom XML through a save; Quill writes only what it models, so anything else in a file is dropped on save (it warns about tables, pictures, notes and comments it couldn't read, not about fields, content controls, charts' data or settings).

### 2.3 Layout

- ✅ **Justification** — `AL_JUSTIFY`.
- 🟨 **Hyphenation** — automatic, Liang patterns, British English only (core/hyph.c, dict/hyph_en_GB.dic), fixed zone. WordCraft: English, Polish, Portuguese, Italian, German, Russian, Turkish patterns or rules, zone/caps/limit honoured, manual hyphenation.
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
- 🟨 **Tables across pages** — rows break, heading rows repeat ✅; floating tables ⬜.
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
- ⬜ **AutoCorrect** — WordCraft: a replacement list with options (two initial caps, capitalize sentences, cells, days, Caps Lock) and exceptions (cmd/tools/autocorrect.rs). Quill none.
- 🟨 **AutoFormat as you type** — Quill: Return in an empty list item ends the list, Tab/Shift-Tab change the level. Missing: smart quotes, dashes, fractions, ordinals, *bold*/_italic_, automatic bulleted and numbered lists from `* ` and `1. `, border lines from `---`, automatic links (WordCraft cmd/text/autoformat.rs).
- ➖ **AutoComplete** — WordCraft doesn't have it either.
- 🟨 **Spelling** — British English (SCOWL word list, core/spell.c), wavy lines as you type, suggestions ranked by typing errors, Change/Change All/Ignore/Ignore All/Add, your own words in Quill.words, right-click suggestions. Missing: other languages and per-run languages, "ignore words in capitals / with numbers / addresses" options, turning checking as you type off, marking text "do not check".
- ⬜ **Grammar** — WordCraft: repeated word, a/an, sentence capital, space before punctuation, double space (more per language). Quill none.
- ⬜ **Thesaurus** — WordCraft has a small built-in one (cmd/review/proofing.rs) and synonyms on the right-click menu. Quill none.
- ✅ **Word count** — pages, words, characters with and without spaces, paragraphs, lines (`word_count()`); live count on the status bar.
- ⬜ **Read Aloud** — WordCraft speaks through the system's voices (crates/engine/src/speech). Not network: on the Amiga, translator.library and narrator.device could do it.
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
- 🟨 **Right-click menus** — Quill (`ID_CTXPICK`): misspelled word, picture/shape/group, text (with table Insert/Delete/Merge/Split/Properties), link. Missing against WordCraft: synonyms, Paste options on the text menu, field menu (Update Field, Edit Field, Toggle Field Codes), table of contents menu (Update, Remove), picture menu's Change Picture, Reset Picture, Insert Caption, table menu's Distribute, AutoFit, Text Direction and Insert Caption (the equation menu has Edit Equation... since WP14a).

### 2.5 Interface: panes, toolbars and the window

- ✅ **Ribbon with contextual tabs, split buttons, galleries, bubble help** — amiga/ribbon.c. (Quill also has an Amiga menu bar, and a Header & Footer and an Edit Text contextual tab where WordCraft puts those tools on Insert.)
- ✅ **Quick access buttons** — Save, Undo, Redo on the tab strip (`g_undo`); not customizable.
- ⬜ **Ribbon collapse** — WordCraft `ui.collapseRibbon`; Quill only hides everything with Focus.
- ⬜ **KeyTips (Alt/F10 letters on the ribbon)** — WordCraft crates/ui-egui/src/keytips.
- ⬜ **Mini toolbar on a selection** — WordCraft crates/ui-egui/src/mini_toolbar.
- ⬜ **Command search (Tell Me, Alt+Q)** — WordCraft `tools.search`.
- ✅ **Status bar** — page, words, language, Focus, view buttons, zoom slider (panes.c).
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
- ⬜ **Proofing** — AutoCorrect Options, ignore capitals/numbers/addresses, check spelling as you type on/off, mark grammar, custom dictionary editor, dictionaries folder and per-language dictionary status. Quill: none of these settings (the custom dictionary exists as Quill.words but can't be edited in Quill).
- 🟨 **Save** — AutoSave ✅ (File › Options, WP24); personal information removed through Inspect Document ✅, not automatically on each save ⬜.
- 🟨 **Language** — interface language ✅ (5 languages); proofing language ⬜; region ⬜.
- ⬜ **Accessibility** — animation effects, Read Aloud voice and speed, skip citations.
- 🟨 **Advanced** — measurement units: Quill cm/in ✅ (WordCraft also mm, pt, pi); alignment guides ⬜; recent documents count ⬜ (Quill keeps a fixed number); region and decimal separator ⬜; KeyTips ⬜; caret animation ➖; user templates folder ⬜; customize keyboard and ribbon ⬜.
- ➖ **Agents** — MCP and control channel: left out on purpose.
- ✅ **Window and screen (Quill only)** — public screen and window placement; nothing to match in WordCraft.

### 2.7 Keyboard shortcuts

WordCraft binds 126 commands to keys (counted from the `.key(...)` of every `CommandSpec` in crates/engine/src); Quill has about 45 (README "Keys", menu shortcuts in main.c `menus[]`, view.c `key()`). Amiga conventions differ (Amiga-L selects all, Amiga-A is Save As, Ctrl-Left/Right go to the line's ends, Alt-Left/Right by words), so this lists functions, not identical keys.

- ✅ **Present in Quill:** cursor movement by character, word (Alt), line end (Ctrl or Home/End), paragraph (Alt-Up/Down), screen, document; Backspace/Del and by word; Return, Shift-Return; Tab and Shift-Tab (lists, cells); Esc; New, Open, Save, Save As, Close, Quit, Next Window; Export PDF; Undo, Redo, Cut, Copy, Paste, Select All; Find, Find Next; Bold, Italic, Underline, Superscript, Subscript; Align left, centre, right, justify; single, 1.5 and double spacing; indent and outdent; page break; grow and shrink font; formatting marks; spelling; insert table; edit header; fit page width.
- ⬜ **Missing (by WordCraft command):** Copy/Paste Formatting (`edit.copyFormat`/`edit.pasteFormat`), Paste Special (`edit.pasteSpecial`), Paste Text (`edit.pasteText`), Repeat F4 (`edit.repeat`), Go Back Shift+F5 (`edit.goBack`), Go To F5 (`edit.goto`, the command exists without a key), Spike and Insert Spike (Ctrl+F3, Ctrl+Shift+F3), field keys (F9 update, Shift+F9 toggle, Ctrl+F9 insert field, Alt+F9 all codes, F11/Shift+F11 next/previous field, Ctrl+F11/Ctrl+Shift+F11 lock/unlock, Ctrl+Shift+F9 unlink), Change Case Shift+F3, Double Underline, Word Underline, Small Caps, All Caps (command exists without a key), Strikethrough (exists without a key), Hidden, Font dialog Ctrl+D (exists without a key), font name and size boxes (Ctrl+Shift+F/P), grow/shrink by 1 pt (Ctrl+]/[), Bookmark Ctrl+Shift+F5, Equation Alt+=, Link Ctrl+K, outline keys (Alt+Shift+1-9, A, +, -), Add Space Before Ctrl+0, Bullets Ctrl+Shift+L, Distributed Ctrl+Shift+J, Hanging Indent Ctrl+T and Remove Hanging Ctrl+Shift+T, Heading 1-3 Ctrl+Alt+1-3, Normal Ctrl+Shift+N, Footnote Ctrl+Alt+F, Read Aloud, Thesaurus Shift+F7, Track Changes Ctrl+Shift+E, Column Selection Ctrl+Shift+F8, Extend Selection F8, Style Inspector Shift+F1, Styles pane, Column Break Ctrl+Shift+Return, © ® ™ … — (Ctrl+Alt+C/R/T/., Ctrl+Alt+-), Nonbreaking Hyphen and Space, Optional Hyphen, Draft/Outline/Print Layout views (Ctrl+Alt+N/O/P), Split Ctrl+Alt+S, Macros Alt+F8 (➖), Search Alt+Q, Move paragraph up/down and promote/demote (Alt+Shift+arrows).
- ⬜ **Customize Keyboard** — WordCraft keeps a user key map (engine/src/keymap.rs). Quill none.

### 2.8 Interface and proofing languages

- 🟨 **Interface languages** — WordCraft offers 27 (en, zh-hans, zh-hant, ja, ko, uk, es, pt-br, sr, sr-latn, pl, cs, et, de, ru, tr, it, hu, fr, bg, vi, sv, hr, nl, da, ro, sl; crates/ui-egui/src/i18n.rs, catalogs in storytold/craft-i18n). Quill offers 5: English, German, French, Spanish, Italian (amiga/lang.c `ui_languages`, amiga/lang_table.h). Missing 22. Quill's interface fonts are Latin-1, so Dutch, Swedish, Danish and Brazilian Portuguese need only translations. Polish, Czech, Croatian, Hungarian, Slovenian, Romanian, Estonian, Serbian (Latin) and Turkish need Latin-2 (or Latin-5/Latin-4) screen fonts. Russian, Ukrainian, Bulgarian and Serbian need Cyrillic fonts. Chinese, Japanese, Korean and Vietnamese are out of reach of the current font pipeline.
- 🟨 **Menus follow the language change only after a restart** — README 0.2 (WordCraft changes everything at once).
- 🟨 **Proofing languages** — Quill: British English spelling and hyphenation. WordCraft: English (US, GB, CA spellings, Moby list), Russian, Polish, Portuguese, Czech, Italian, Turkish, Indonesian, Spanish (system Hunspell dictionaries through its own affix engine), grammar rules per language, hyphenation for English, Polish, Portuguese, Italian, German, Russian and Turkish. Quill has no Hunspell affix reader, no per-run languages, no grammar.

### 2.9 Zotero

- ➖ **Zotero integration** (WordCraft crates/zotero, the Zotero tab, docs/zotero.md) — talks to the Zotero desktop app through its local HTTP connector; there is no Zotero for AmigaOS. What Quill should still do is keep Zotero's and Mendeley's `ADDIN ZOTERO_ITEM`/`ADDIN ZOTERO_BIBL` fields and their results through a save, so a document can go back to a machine with Zotero: that is part of WP1 (fields).

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
