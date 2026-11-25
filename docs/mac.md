# Octavo: Quill for the classic Mac OS

**Octavo** is the classic Mac OS edition of Quill, the Amiga word processor this repository is
about. It has its own name because a classic Mac program called Quill exists already; the Amiga
version stays Quill, and the core, repository and branch keep that name. (A modern macOS app called
Octavo, a PDF booklet-imposition tool on the Mac App Store, is a different program of another era; no
classic Mac OS program of that name turned up in the archives searched.) Below, Octavo means the Mac
program and Quill the Amiga one or the shared core. Its creator code is `OCTV`.

Octavo runs on a Macintosh with a 68030 or 68040 and System 7: it opens a Word document, shows its pages
as Quill's core draws them for PDF, lets you type and format, and saves .docx again. This is the story of the
port, how to build and install it, what it does and what it does not do yet. Started 18 November 2026 on
branch `mac-port`.

![Octavo on a 1-bit screen, the Style menu down](mac-1bit.png)

## How it is made

Quill's core (`core/`) is portable C99 and is used unchanged. The Amiga front end (`amiga/`, about
50,000 lines) is not: it draws text itself with Amiga bitmap fonts made from Quill's TrueType fonts,
through graphics.library, layers and diskfont, and its ribbon, panes and dialogs are hundreds of MUI
objects of kinds the Mac's MUI does not have. So the Mac got a front end of its own, small and in MUI
too (`mac/`), which runs on Riptide's MUI for the Mac (riptide `src/mac/mui`, docs/MAC-GUI.md there):
Application and Window objects, a NewMenu strip, MUI_RequestA and the ASL requester become the Mac's
own window, menu bar, alerts and Standard File, laid out as Apple's Human Interface Guidelines (1992)
ask. The same Riptide tree builds RiptidePrefs and Lagoon for the Mac.

| File | What |
|---|---|
| `mac/qm_main.c` | the application: the window, the menus and their check marks, files, the Edit menu, the commands |
| `mac/qm_view.c` | the page view, a MUI Area subclass: drawing, scrolling, the caret and selection, keys and clicks; the strip and rulers inside its box |
| `mac/qm_qdtext.c` | the page's text drawn with QuickDraw in the Mac's own screen fonts (Times, Helvetica, Courier ...), at the layout's places ("Text in the Mac's own fonts" below) |
| `mac/qm_strip.c` | the tool strip (View › Ribbon): pop-up menus and icon buttons, each a menu command |
| `mac/qm_ruler.c`, `core/ruler.c` | the rulers (View › Ruler): drawing and dragging on the Mac; the sums, shared and tested natively |
| `mac/qm_lang.c` | `tr()`: Quill's interface words (`amiga/lang_table.h`) in Mac Roman, the language from the System's region |
| `mac/qm_text.c` | Mac Roman to and from UTF-8 and Latin-1 |
| `mac/libc.c`, `mac/include/` | the C library the core needs (allocator, stdio over the File Manager, time, strings, sorting) |
| `mac/softfp.c` | integer and float conversions without an FPU, in place of libgcc's 68881 ones ("Floating point" below) |
| `mac/qm_mac.c` | the File Manager by path, full paths of folders and files (`qm_dir_path`, `qm_spec_path`) and of Octavo's own folder, file types and creator, FlushVol, the clock, the log, the System's region |
| `mac/qm_thunks.S` | the class dispatcher's register entry and 32-bit QuickDraw's offscreen calls |
| `mac/traps.txt` | the Toolbox calls Octavo adds to Riptide's list (CopyBits, Draw1Control, EventAvail, resources) |
| `mac/mkfinder.py`, `mac/rsrc/` | the Finder's resources: bundle, file references, icons in every size and depth, 'vers' |
| `mac/mksize.py` | the SIZE resource's flags: high-level-event aware, so the Finder sends Open Documents |

### Pages on the screen, 1 to 32 bits

(Since 23 November 2026 the page's text is drawn by QuickDraw in the Mac's own fonts and only the rest
comes from Quill's rasteriser: "Text in the Mac's own fonts" below. View › Smooth Text draws it all as
described here.) For each page it shows the view asks the core for the page as pixels (`pdf_page_rgb`, core/raster.c: exactly what Export PDF writes, drawn with Quill's own TrueType
rasteriser in fixed point), at the zoom's resolution: 72 dots per inch at 100%, the Mac screen's own, so
a 12-point letter is 12 pixels high. It keeps the last three pages drawn. Everything that reaches the
window goes through one routine, `blit()`: a rectangle of one page converted a band of 32 rows at a time
into an offscreen 32-bit GWorld, with the selection and the caret mixed in, and copied with CopyBits.
CopyBits fits the colours to the screen, whatever its depth: on 1-bit and indexed screens with
`ditherCopy`, on 16 and 32 bits as they are. On a 1-bit screen each level is stretched first (72 and
darker black, 200 and lighter white), because smoothed text edges dithered as they are come out speckled;
pictures keep their dithered shades.

charts.docx was also opened at 1, 2, 4, 8, 16 and 32 bits (18 November 2026, PiDepth setting the depth):
the charts are drawn at every depth, dithered below 16 bits. The page box beside the horizontal scroll bar
shows the page at the top of the window, as Word for the Mac did, and the number of pages.

Only what changed is copied. An update event (a window uncovered) redraws the visible region's box; the
caret blinks by copying its one-pixel column again; a selection that grows redraws the rows between the
old and new end. After an edit the page is drawn again in memory, compared row by row with the picture it
replaces, and only the rows that differ are copied. Typing does not draw a page per key: the keys change
the document at once, and the page is drawn again when no more keys are waiting (`EventAvail`), or after
a second at the latest. Scrolling moves what is still visible with ScrollRect and draws only the strip
that came into view. The watch shows while a page is drawn.

| Screen | Screenshot (testbed, System 7.1, 1024 by 768) |
|---|---|
| 1 bit | [mac-1bit.png](mac-1bit.png): the document after typing, Bold, Save, Close and Open; the Style menu down |
| 8 bits | [mac-8bit.png](mac-8bit.png): tests/data/word.docx just opened |
| 32 bits | [mac-32bit.png](mac-32bit.png): text typed at the start, five letters selected and made bold |
| 8 bits | [mac-charts-8bit.png](mac-charts-8bit.png): tests/data/charts.docx just opened, "Page 1 of 3"; [mac-charts-8bit-page2.png](mac-charts-8bit-page2.png): scrolled to page 2 |

### Floating point: soft-float

The Mac build is compiled `-msoft-float` with libgcc's routines, not `-m68881`. Reasons:

- It runs on every 68030 and 68040 Mac. The LC II, LC III, Classic II, Color Classic and Performa 400
  have no FPU, the LC 475/Quadra 605 and the Performa 47x have a 68LC040, and a `-m68881` program dies
  with an F-line exception on all of them (Apple shipped no FPU emulation).
- Nothing that runs while typing uses it. The layout (`core/layout.c`: no `double` at all), the
  TrueType rasteriser and the page drawing (`core/raster.c`) are integer arithmetic, fixed point. In
  `core/` doubles appear in charts (`chart.c`), a few file readers and writers (`docx.c`, `odt.c`, `rtf.c`,
  `html.c`) and SmartArt, used briefly when such a document is read or a chart drawn.
- Not measured on hardware: what an FPU would gain for charts. A `-m68881` build is one flag away
  (`BASE` in Makefile.mac) for a Mac known to have one.

libgcc's soft-float code (built position-independent for Linux) reaches its rounding-mode variable through
a global offset table; Riptide's `tools/mac_elf.py` now relocates that table's entries.

**libgcc's conversions are FPU code (found 18 November 2026).** Debian's and Ubuntu's `m68k-linux-gnu`
compiler has one libgcc, built for a 68020 with a 68881 (`-print-multi-lib` shows only `.`). Its double
arithmetic and comparisons (`_double.o`, from `lb1sf68.S`) are integer code that take and give doubles in
D0/D1, as `-msoft-float` code expects. Its conversions are not: `fpgnulib.o` (`__floatsidf`, `__fixdfsi`,
`__extendsfdf2`, `__truncdfsf2` and six more) is C compiled for the 68881. It uses FMOVE and returns in FP0. So
every `(double)i` in the core gave whatever D0/D1 held. Charts showed it first: a chart's 4.3 was read as
2.7e+153 and every chart was drawn as an empty box. With an FPU (the testbed's 68040) the result was wrong. On
a Mac without one, the FMOVE would have stopped Octavo with an F-line exception. `mac/softfp.c` now defines all
ten functions in integer arithmetic, rounding to nearest, ties to even. Makefile.mac fails the build if the map
shows `fpgnulib.o` linked again. Tested:
- natively, against a PC's FPU, 20 million random and edge values, all identical (the file compiles for either
  byte order);
- the core built `-m68020 -msoft-float` with it, run under qemu-m68k: charts.docx's first page is identical,
  pixel for pixel, to the native build.

The earlier check with word.docx had passed only because nothing on that page converts an integer to a double.

### Speed

Drawing one US Letter page at 72 dpi in the testbed (QEMU emulating the Pi 3 that runs Emu68, many times
slower than real hardware): an empty page 82 ticks (1.4 s), tests/data/word.docx's first page 200 to 290
ticks (3.3 to 4.8 s). On the host the same page takes 2 ms. The log (`make -f Makefile.mac AURORA68=1`)
prints each page's time. A 25 MHz 68030 should be around a second a page; that is why typing is drawn
once the keys stop rather than per key, and why only changed rows are copied.

## The tool strip and the rulers

The Amiga's Quill has WordCraft's ribbon. System 7 has no ribbon, and Cornerstone's classic Mac chapter
(the family's style guide, `docs/platforms/classic-mac/README.md` in Cornerstone) says what becomes of it:
the ribbon's most used tools go into one row in the document window under the title bar, its drop-down
lists become pop-up menus, and everything stays in the menus. Word for the Mac had such a row and called
it the Ribbon, so the View menu does too.

![The tool strip and the rulers on an 8-bit screen](mac-ribbon-8bit.png)

**The strip** (`mac/qm_strip.c`) holds what the Amiga's Home tab is used for most: the paragraph style,
font and size as pop-up menus; Bold, Italic and Underline; the four alignments; bulleted and numbered
lists; Decrease and Increase Indent. Each item is a menu command (Format › Paragraph Style, Font, Size,
Style, Format) with the same number; a click runs that command, and the item draws itself from
`qm_state()`, the one answer the menus' check marks and dimming are set from, so a button and its menu
item never disagree. A pop-up shows its menu's items with the current one checked; its box shows the
caret's value even when the menu has no such item (Calibri, 11 points). The icons are the Amiga's
(`amiga/theme.c`, Cornerstone's icon language), drawn with QuickDraw in Geneva 9's strip: black and white
first (a button that is on is inverted, one held down inverts while the pointer is on it), coloured from
four bits up, with B, I and U in bold Times as Word's were. The whole strip is about 540 pixels wide, so
it fits a 640-pixel window; a narrower window drops whole groups from the right (indents, then lists,
alignment, character styles), whose commands stay in the menus. Show ¶ is left out: the page view draws
pages as their PDF looks (`pdf_page_rgb`), which has no paragraph marks to show.

**The rulers** (`mac/qm_ruler.c`, with the sums in `core/ruler.c`, shared and tested natively in
`tests/test_wpmac.c`): the horizontal ruler under the strip lines up with the page as it scrolls and
zooms, in inches on a US Letter page and centimetres on any other (as the Amiga decides), the margins
shaded and the text area white, numbered from the text's edge. The first-line indent hangs from the top,
the left and right indents stand on the bottom, and the caret paragraph's tab stops show; in a table the
column edges show as blocks. Dragging a marker is tracked as the Mac tracks a control: the ruler follows
and a dotted line over the pages shows where it will be; the change is made when the button goes up, as
one undoable edit (the indents or tab stops of the selected paragraphs, or the table's column widths:
the Amiga's CMD_INDENTS, CMD_TABS and CMD_COLEDGE). A click on the lower half makes a tab stop of the
kind the box at the corner shows (click the box for left, centre, right, decimal); a stop dragged off the
ruler goes. The vertical ruler on the left shows the page's top and bottom margins, as on the Amiga
(there, too, it does not drag).

| | |
|---|---|
| ![1 bit](mac-ribbon-1bit.png) | ![32 bits, the style pop-up](mac-ribbon-32bit.png) |
| ![A 640 by 480 screen](mac-ribbon-640.png) | ![A narrow window: groups dropped](mac-ribbon-narrow.png) |
| ![The left indent dragged](mac-ruler-drag.png) | ![A table's column edge dragged](mac-ruler-table.png) |
| ![The font pop-up](mac-ribbon-fontpop.png) | ![4 bits, the ruler hidden by the preferences](mac-ribbon-4bit-view.png) |

View › Ribbon and View › Ruler are checkmarked items (Guidelines p. 75), kept in **Octavo Preferences**
(type `pref`) in the Preferences folder. The pages' area, the scroll bars, updates and the caret all keep
to what is left: the vertical scroll bar starts under the ruler.

## Switchable strips: one per tab, and the commands behind them

The user asked to change the strip for each kind of work, as the Amiga changes ribbon tabs. Agreed
design: a pop-up at the left end of the strip chooses it (Home, Insert, Layout, References, Review),
the same choices as View › Tool Strip (a submenu, the current one checked); View › Ribbon still shows
and hides it; the choice is kept in Octavo Preferences (`strip N`). Every item is a menu command first
(Cornerstone chapter 3): the menus Insert, Layout, References and Review follow Format in the
Amiga's tab order, with submenus for the long lists; each command's state (usable, on) is the one
answer `qm_state()` gives menus and strips alike, and a command that can't be used here is dimmed,
not hidden (a table command outside a table, Next Comment without comments).

Proved in the testbed at 8 bits on a 1024 by 768 screen (19 November 2026), each strip doing its work:

| | |
|---|---|
| ![The switcher open](mac-strip-switcher.png) The switcher: Home checked | ![Home](mac-strip-home-8bit.png) Home, with Font… and Find at its end |
| ![Insert](mac-strip-insert-8bit.png) Insert: a picture at the start, a 3 x 2 table from Table… | ![Layout](mac-strip-layout-8bit.png) Layout: Page Setup made the page landscape |
| ![References](mac-strip-references-8bit.png) References: Table of Contents put in a table of contents | ![Review](mac-strip-review-8bit.png) Review: Track Changes on, "Tracked" typed |
| ![Insert Table](mac-dialog-table.png) Insert Table | ![Page Setup](mac-dialog-pagesetup.png) Page Setup |
| ![Find and Replace](mac-dialog-find.png) Find and Replace | ![New Comment](mac-dialog-comment.png) New Comment |

At 1 bit on a 640 by 480 screen (all eleven menu titles fit in English; the window is 632 wide, so
Home's Font… and Find drop off its end), the five strips as the switcher shows them, the spelling
dialog and the About box:

![The five strips at 1 bit, 640 pixels](mac-strips-1bit-640.png)

| | |
|---|---|
| ![Home at 640 by 480, 1 bit](mac-strip-640-1bit.png) | ![Spelling](mac-dialog-spelling.png) |
| ![About Octavo](mac-about-octavo.png) | |

| Strip | Items (each a menu command) |
|---|---|
| Home | style, font and size pop-ups; Bold, Italic, Underline; four alignments; Bullets, Numbering; Decrease, Increase Indent; Font…, Find… |
| Insert | Table…, Picture…; Page Break, Breaks ▾; Footnote…, Endnote…; Date, Page Number, Symbol ▾ |
| Layout | Page Setup…, Portrait, Landscape; Columns ▾, Breaks ▾, Line Numbers ▾; Paragraph…, Single, 1.5 Lines, Double; Decrease, Increase Indent; Insert Rows Above, Below, Columns Left, Right, Delete Rows, Merge Cells, Split Cells |
| References | Table of Contents; Footnote…, Endnote…, Next Footnote; Caption…, Bookmark…, Cross-reference… |
| Review | Spelling…, Word Count; New Comment…, Previous, Next, Delete Comment; Track Changes, Previous, Next Change, Accept, Reject |

A ▾ item opens its submenu as a pop-up menu. In a window narrower than a strip, whole groups drop from
the right end (Layout's table tools first); the menus keep everything.

**How it is built.** The document work is the core's, in `core/edcmd.c`: what the Amiga's page view
does inline for each command (`amiga/view.c` CMD_TABLE, CMD_SECTBREAK, CMD_PICTURE, CMD_FINDX,
CMD_RESOLVE, CMD_NEXTCOMMENT...), moved where the Mac calls it on its selection and the native tests
check it (`tests/test_wpmac.c`, about 80 checks, each command's .docx round trip included). The Mac's
side (`mac/qm_cmds.c`) has the menus' states, the commands and their dialogs. The dialogs
(`mac/qm_dialog.c`) are the Dialog Manager's, built in memory as Riptide builds its alerts: no title
bar, the prompt at the top, the action button at the bottom right with Cancel to its left, the default
button ringed and answering Return and Enter, Cancel answering Escape and Command-period, check boxes
and radio buttons, text fields with Tab between them, a third of the way down the screen.

- **Insert:** Table… asks columns and rows; Picture… is Standard File, any file the core reads as PNG
  or JPEG, narrowed to the column; the breaks; Footnote… and Endnote… ask the note's text (one
  paragraph: the Mac does not edit notes in place as the Amiga does); Date and Page Number are fields;
  Symbol puts in a character.
- **Layout:** Page Setup… sets the whole document's paper (US Letter, A4, US Legal, A5), orientation
  and margins in the ruler's unit; Columns and Line Numbers set sections as the Amiga's commands do;
  Paragraph… sets indents, spacing before and after, and line spacing. Hyphenation is left out: the core
  has it, but the Mac does not load its patterns yet.
- **Find and Replace** (Edit): one modal dialog (Find, Replace with, Match case, Whole words only;
  Replace All, Replace, Cancel, Find Next); Find Again repeats it. **Format › Font…**: the families,
  the size, the six styles.
- **References:** Table of Contents (inserts, or brings the one there up to date), Footnote, Endnote,
  Next Footnote, Caption… (Figure, Table, Equation), Bookmark… (on the selection or the word at the
  caret) and Cross-reference… (to a bookmark, as a REF field).
- **Review:** Spelling… with Quill's British English word list (`dict/british-english`, in a folder
  named dict beside Octavo): each unknown word selected and asked about, up to five suggestions,
  Change, Ignore All, Cancel. Word Count. Comments: New Comment… asks the text (author: the System's
  owner name); Next and Previous select a comment's range and show its text in a note (the page view
  does not draw comments' balloons). Track Changes: while it is on, typing, Delete, Cut, Clear and
  pasted text are the author's tracked changes (insertions underlined, deletions struck through on the
  page), and formatting changes are tracked too; Accept, Reject, and both for all changes.

**Against the Amiga's ribbon.** Of the commands `docs/wordcraft-gap.md` marks done on the Amiga, the Mac
has, through its menus and strips:

| Amiga tab | Amiga ✅ | Mac | The Mac's |
|---|---|---|---|
| Home | 113 | 34 | Paste, Cut, Copy, Font, Font Size, Increase and Decrease Font Size, Bold, Italic, Underline, Strikethrough, Subscript, Superscript, Font Dialog, Bullets, Numbering, Decrease and Increase Indent, Align Left, Center, Align Right, Justify, Line Spacing, Paragraph Dialog, Tabs (the ruler), Normal, Heading 1-3, Find, Find Next, Replace, Replace All, Select All |
| Insert | 47 | 9 | Page Break, Insert Table, Pictures, Bookmark, Cross-reference, Comment, Page Number, Date & Time, Symbol (section and column breaks are counted under Layout's Breaks) |
| Layout | 23 | 9 | Margins, Orientation, Size, Columns, Breaks, Line Numbers, Page Setup Dialog, Indent, Spacing |
| References | 24 | 7 | Table of Contents, Update Table, Insert Footnote, Insert Endnote, Next Footnote, Insert Caption, Cross-reference |
| Review | 28 | 13 | Spelling (no grammar), Word Count, New Comment, Delete, Previous, Next, Track Changes, Accept, Reject, Accept All, Reject All, Previous Change, Next Change |
| Table Layout | 33 | 9 | Insert Above, Below, Left, Right, Delete Rows, Columns, Table, Merge Cells, Split Cells |
| **These six** | **268** | **81** | |

## Files, the Mac's own fonts and the compact Mac's screen (23 November 2026)

The owner asked for three things: Octavo must open files (it looked in the folder above the right one),
its text must look like MacWrite 4's rather than smoothed TrueType thresholded to one bit, and it must work
on the SE's own screen, 512 by 342 pixels at one bit. Branch `octavo-se`, tested in the Aurora68 testbed
(QEMU, the solo board, System 7.1) at 512 by 342 and 640 by 480 at one bit and 640 by 480 at eight bits
(not yet on a large screen with this branch), driven through the Aurora68 remote control and its agent (docs/13 in the Aurora68 project:
`open` launches a program or opens a document the way the Finder does, with an Open Documents event).

### Opening files: the folder above the right one

**Reproduced.** The test disk had Octavo in `Apps:Octavo Folder`, a copy of tests/data/word.docx as
`Documents:Brief für Zoë:Letter été.docx` (a space and Mac Roman letters in both names) and a second volume,
`Other Disk`, on the card with `Reports:Year 2026:Report • draft.docx`. File › Open on the nested document
answered "Mac Volume:Documents:Brief für ZoëLetter été.docx couldn't be read"; opening it from the Finder
(the agent's `open`, which launches Octavo with it) logged "the Finder opens Mac Volume:Documents:Brief für
ZoëLetter été.docx" and showed an empty window. Every document tested before sat at the top of the disk,
where the bug does not show.

**The cause.** Standard File's reply and the Finder's documents name a file by volume, folder and name;
Octavo made a full path of the folder with Riptide's `mm_dir_path` and put the name after it. That function
gives "Disk:Folder:Sub", with no colon at the end (except for a volume's top, "Disk:"), so the name ran on
into the folder's: "Disk:Folder:Subname" is a file called "Subname" in the folder above. The Fonts and dict
folders had a weaker spot of their own: they were found as `:Fonts:` and `:dict:`, from the default folder,
which is Octavo's only when the Finder made it so at launch, and which Standard File moves.

**The fix** (mac/qm_mac.c, mac/qm_main.c): `qm_dir_path` builds a folder's full path from its end, folder by
folder with PBGetCatInfo, always ending in ':', to any depth (the File Manager's 255 characters at most;
Riptide's stopped at 16 levels); `qm_spec_path` adds a file's name. Standard File's replies, the Finder's
documents and the preferences folder all go through them. Octavo's own folder comes from its own file:
PBGetFCBInfo on its resource fork (CurApRefNum) says which volume and folder it is in, and the Fonts and dict
folders are looked up there. Riptide is unchanged (its own callers put a ':' between with AddPart).

**Apple events.** Octavo is now high-level-event aware (mac/mksize.py sets the SIZE resource's flag after
Riptide's elf2app.py has written it) and handles the required events: Open Application, Open Documents
(the first document; one window, one document, so the open one is asked about first), Print Documents
(opened: Octavo does not print) and Quit (as File › Quit, asking to save). Riptide's event loop does not
look at high-level events, and an event not accepted stays queued, so Octavo's main loop takes them before
each turn of MUI's (WaitNextEvent for high-level events only, then AEProcessAppleEvent); the Apple Event
Manager's calls and the handler's Pascal entry are in mac/qm_thunks.S. So a document double-clicked or
dropped on Octavo's icon while it runs is opened now; before, only the Segment Loader's list at launch was
read.

| | |
|---|---|
| ![Before: the nested document "couldn't be read"](mac-open-before.png) Before (640 by 480): File › Open on a document two folders down | ![After: opened from the Finder](mac-open-after-512.png) After (512 by 342): the same document opened from the Finder, the window titled with its name |
| ![After: the second volume](mac-open-other-disk.png) After: `Other Disk:Reports:Year 2026:Report • draft.docx` by File › Open | |

**Tested** (512 by 342, one bit): the nested document opened by the agent's `open` while Octavo was not
running (logged "an Open Documents event for Mac Volume:Documents:Brief für Zoë:Letter été.docx"); File ›
Open of `Other Disk:Reports:Year 2026:Report • draft.docx` on the second volume; `Documents:charts.docx`
sent to the running Octavo ("running already: the document sent to it"), which opened it; Review ›
Spelling… after Standard File had been in other folders, its word list read from `Octavo Folder:dict`; the
fonts read from `Octavo Folder:Fonts` ("Octavo's folder: Mac Volume:Apps:Octavo Folder:" in the log).
There is no Open Recent on the Mac, so nothing to test there. Not tested: two volumes of the same name (a
full path reaches the first), a path longer than 255 characters (refused with a log line), dropping onto
the icon in the Finder by hand (the testbed has no mouse at the Finder's icon in a script; the agent sends
the same event the Finder does).

### Text in the Mac's own fonts

**Before.** The view showed each page as Quill's PDF export draws it (`pdf_page_rgb`, Quill's TrueType
rasteriser, smoothed), and on a one-bit screen the grey edges were stretched and dithered: uneven stems,
letters that run together or fall apart at 11 points.

**What was decided, and why.** Two ways were open. (1) Lay the document out with the Mac fonts' widths (a
metrics provider for the core): the screen would be exactly the Mac's, but lines and pages would break
differently on the Mac than in the PDF Octavo exports, in Word and on the Amiga, and the core's layout and
its 2,600 tests would have a second set of metrics to keep. (2) Keep the core's layout (Quill's TrueType
metrics, as now) and draw each word at the place it gives, in the Mac's font: Word for the Mac with
fractional widths off drew so. Octavo does (2): line breaks, page breaks, every word's place, the PDF and
the .docx stay what they were; within a word the letters have the bitmap font's own spacing, and between
words the layout's spaces take up the difference. The core changed only by a hook (`pdf_page_rgb_text`,
core/pdf.c, tested in tests/test_wpmac.c): it draws the page's picture without the runs of text the front
end takes and hands each over with its place, width, size, colour, underline and strike.

**How** (mac/qm_qdtext.c):
- *The family.* The document's own when the Mac has it (Palatino, Geneva, Times…); else a table: serif
  families (Times New Roman, Cambria, Liberation Serif, Georgia…) become Times, sans serif ones (Arial,
  Calibri, Liberation Sans, Segoe UI…) Helvetica, typewriter ones (Courier New, Consolas, Liberation Mono)
  Courier; else by the face the layout used. Times, Helvetica and Courier were chosen over New York, Geneva
  and Monaco because Times New Roman, Arial and Courier New were drawn to their widths. Bold, italic,
  outline and shadow are QuickDraw's faces; underline (single, double, thick, dotted, dashed, wave) and
  strikethrough are one-pixel lines drawn across the layout's width, so underlined words and the spaces
  between them make one line.
- *The size.* Each family, size and style is measured once, from a page's runs, at the size asked and
  down to a fifth smaller: the runs' widths in the Mac font against the layout's. Wider costs twice what
  narrower does (a word wider than its room has its letters closed up; a narrower one only leaves a wider
  space), a bitmap size the System has is let off 3 per cent, and the cheapest wins. The Font Manager's own
  table (the 'FOND') says which bitmap sizes there are (Times, Helvetica and Courier: 9, 10, 12, 14, 18
  and 24) and whether there is a TrueType outline (System 7.1 has one for each, drawn unsmoothed). In
  tests/data/word.docx at 100 %: Calibri 26 → Helvetica 24 (bitmap, 1000 ‰ of the layout's width), Cambria
  11 → Times 11 (outline, 1024 ‰), Cambria 14 → Times 14 (bitmap, 1023 ‰), Calibri 14 bold → Helvetica 11
  bold (930 ‰): Helvetica's letters are wider than Calibri's, its bold ones more so.
- *Words that don't fit.* A run wider than its room by a pixel (two from 16 pixels up) has each letter
  drawn at its place scaled to the layout's width; a single letter wider than its room (the article "A")
  takes half the excess from the space on each side.
- *Drawing.* The runs are drawn with QuickDraw into a one-bit bitmap of the page's size in a GrafPort of
  Octavo's own, one colour at a time, and each black pixel is put into the page's picture in that colour.
  On a one-bit screen every run is black (white when it is nearly white), so coloured headings come out
  solid instead of dithered. The view shows the picture as before, so its caret, selection, row comparison
  and copying did not change; drawing a page got faster (word.docx's first page: 36 ticks in the testbed,
  341 with the TrueType rasteriser).
- *The caret.* The layout puts the caret between letters by Quill's metrics; the view moves it to where
  the letter was drawn (`qt_map_x`), and a click is taken back the other way (`qt_unmap_x`), so the caret,
  a selection's ends and double-clicked words line up with the letters on the screen.
- *Left to the core's rasteriser,* drawn smooth as before: pictures, shapes, charts' and SmartArt's
  drawing, borders, shading; and text QuickDraw cannot draw as the document says: characters Mac Roman lacks
  (Greek, Cyrillic, East Asian, the euro, which System 7's fonts draw as ¤), small capitals, scaled text,
  embossed and engraved text, WordArt, equations, text in turned frames.
- *View › Smooth Text* (kept in Octavo Preferences, `smooth 1`) brings back the old drawing.

**Measured on the screen.** Octavo logs, for every page it draws, how many runs QuickDraw drew, how many
were wider than their room and closed up (and by how much at most), and how much narrower than its room a
run was at most. word.docx's first page: 92 runs, of which 10 (one session at 512 by 342) to 33 (two launches at 640 by
480) were closed up, by at most 3 and 8 pixels (the widest, a quoted word in curly quotes, its letters a
little under a pixel closer each), and at most 5 pixels short; a space at 11 points is 2 to 3 pixels, and
none vanished. QuickDraw's widths of the Times 11 outline differed between launches, which goes with the
wrong letters noted below. charts.docx (Times New Roman 10): 110 runs, none closed up. In the
pictures below the words neither touch nor stand apart; the widest spaces are where Helvetica 24 is
narrower than Calibri 26 ("Quill  Test").

| Before (smoothed TrueType at one bit) | After (QuickDraw, the System's fonts) |
|---|---|
| ![before](mac-text-before-512.png) | ![after](mac-text-after-512.png) |
| ![before, 2x](mac-text-before-zoom.png) | ![after, 2x](mac-text-after-zoom.png) |
| ![before, 640 by 480, 8 bits](mac-text-before-640-8bit.png) | ![after, 640 by 480, 8 bits: the colours kept](mac-text-after-640-8bit.png) |

Once, the first time a newly copied Octavo ran, the Times 11 outline's letters came out wrong in the
testbed ("tex1", "Cefe"); Octavo was quit and started again and they were right, and they stayed right for
the rest of the session. QuickDraw's TrueType code drew them, from the System; whether the emulator (Emu68's
translation cache with a new program at the old one's addresses) or memory was to blame is not known yet.

### The compact Mac's screen, 512 by 342

**Before.** The window fitted (Riptide sizes it to the main screen: 504 by 298), but the menu bar did not:
eleven titles of Octavo's need about 640 pixels, View was cut in half and the Help menu was covered. The
tool strip showed its pop-ups and Bold, Italic, Underline (the narrow-window rule dropped the rest, as
designed).

**After.** The menus fold as Word 5 for the Mac's did on that screen (mac/qm_main.c `menus_fold`): the
titles are measured in the system font with the Menu Manager's 13 pixels each, against the screen's width
less 80 pixels for Help and the Application menu (the testbed measured 67 at 512: the menu bar's own list
is logged at start), and folded step by step until they fit: (1) the sizes go into the Font menu above the
families and References' commands into the Insert menu; (2) Layout's commands follow Format's; (3) the
styles go into the Font menu too. Every command keeps its menu and its key. The titles are measured in the
Window Manager port's font as it is: the testbed's menu bar is Chicago 12 at 512 pixels but a smaller plain
face at 640, and setting that port to the system font to measure (the first try) made the 640-pixel bar
Chicago too and folded it needlessly. At 640 nothing folds, as before (the last title ends at 492, Help at
573). English at 512 needs step 2:
File, Edit, Font, Style, Format, Insert, Review, View, ending at 416 with Help at 445. A long menu on a
342-line screen scrolls, as the Menu Manager scrolls any (Format with Layout's commands has 27 rows). The
dialogs fit: Page Setup is the tallest at 291 of the 314 lines under the menu bar; Format › Font… lists only
as many families as the screen has room for. Alerts (Riptide's) and Standard File are the System's sizes.

| | |
|---|---|
| ![Before: the menu bar cut short at 512](mac-text-before-512.png) Before: View cut, Help covered | ![After: the folded menus](mac-text-after-512.png) After: eight titles, Help and the Application menu clear |
| ![Format with Layout's commands, scrolling](mac-512-format-menu.png) Format, scrolled to Layout's commands | ![Page Setup at 512 by 342](mac-512-pagesetup.png) Page Setup |
| ![The Font menu with the sizes](mac-512-font-menu.png) The Font menu with the sizes | ![About Octavo](mac-512-about.png) About Octavo |

## The experimental ribbon tree (branch `octavo-ribbon`, 25 November 2026)

An experiment, asked for by the owner: an Octavo that looks and works like Quill on the Amiga (the ribbon's
tabs and groups, the File page, the status bar) rather than like a 1992 Mac program, the Guidelines set
aside for this tree; then made as small and as quiet as it can be, measured. It is a branch of its own
(`octavo-ribbon`, from `mac-port`), built against Riptide's branch of the same name (`RIPTIDE=`). Nothing
in `mac-port` changed.

![The ribbon at 640 by 480, eight bits, charts.docx open](mac-ribbon-tree-insert.png)

### Shared, not copied: the Amiga's own files compiled for the Mac

The first aim was to compile the Amiga front end's own sources against Riptide's MUI for the Mac, so that
the ribbon is described once. That worked for everything the ribbon, the status bar and the File page are
made of:

| | Files | How |
|---|---|---|
| **Unchanged** | `amiga/theme.c` (the theme's colours, the interface fonts, every icon), `amiga/panes.c` (the status bar; the navigation pane's class too), `amiga/backstage.c` (the File page), `amiga/wphooks.c`, and the headers `quill.h`, `ribbondef.h`, `ids.h`, `theme.h`, `wphooks.h`, `lang.h`, `wp_*.h` | compiled as they are |
| **One hook** | `amiga/ribbon.c` (2,600 lines: tabs, groups, the lists, the styles gallery, collapsing, the Quick Access Toolbar, KeyTips) | `RIBBON_CAN(id)` in `item_enabled`: 1 on the Amiga; on the Mac, whether Octavo has the command yet (dimmed if not) |
| **Data taken out at build time** | the 39 work packages' `amiga/wp_*.c` (27 of them give the ribbon groups) | `mac/mkwpgroups.py` keeps each file's defines, enums and the data its groups reach (items, lists, label arrays, icon strings), drops its code, and writes a `wpN_package` with no hooks; the generated files compile unchanged against the same headers |
| **Shims** (`mac/ashim/`, `mac/qm_ashim.c`) | the NDK's header names (`<proto/graphics.h>`, `<libraries/mui.h>` ... 44 of them) | one header over Riptide's MUI: the MUI numbers and object macros Quill uses beyond Riptide's, `struct InputEvent`, `DiskFontHeader`; amiga.lib's varargs calls (`DoMethod`, `NewObject`, `SetAttrs` ...), utility.library's tags and `Stricmp`, `FilePart`, `MapRawKey`, `DoubleClick`, `MUI_AddClipping`; the work packages' hooks the Mac has no code for (customizing, Tell Me, the Developer tab, personal templates, AutoSave), answered as a fresh Quill would. Quill's `theme_*`/`tpen` clash with Riptide's own theme of the same names (Riptide draws its classes with it): the shim header renames Quill's to `qt_*`. The dispatchers are written for vbcc's registers (`__reg("a0")`): on the Mac they are C functions reached through `qm_cdispatch_thunk` (`mac/qm_thunks.S`), which takes BOOPSI's A0/A2/A1 off to the C function kept in the class hook's `h_SubEntry` |
| **Added to Riptide** (its `octavo-ribbon` branch) | `src/mac/mui` | fonts by name and size (`OpenFont`, `OpenDiskFont`: a Mac family, the bitmap size whose line is nearest the height asked, bold for a name ending in B), `Text` and `TextLength` in the rastport's font and soft style, `TextExtent`, `TextFit`, `DrawEllipse`, `BltTemplate`; windows without a frame (the ribbon's lists); `MUIA_Window_Activate` notified (a list closes when another window comes forward); MUI 3's `MUI_RequestIDCMP`/`MUIM_HandleInput`; `MUIA_Parent`; IntuiMessages stamped with the time. RiptidePrefs, Riptide and Lagoon build unchanged against it |
| **Written for the Mac** | `mac/qm_ribbon.c` (about 400 lines) | what the Amiga's `main.c` and `view.c` do for these classes: the window's contents (ribbon, page view, status bar in a group; the File page as the other page of a page group), the state they show (`MUIM_Ribbon_State`, `MUIM_Status_Set`, `MUIM_Back_Set` from the document and the caret), and every Amiga command mapped to the Octavo command that does it, or done there on the core (font, size, style, colour and highlight lists, the Layout tab's indent and spacing fields, zoom). The Amiga's ids reach Octavo's loop as `QM_AMIGA_ID` + their number (the shim's `DoMethod` adds it), Octavo's own are in `mac/qm_ids.h` |
| **Kept from Octavo** | the page view, its rulers and scroll bars, the dialogs, the menus | the page view (`mac/qm_view.c`) is Octavo's: the Amiga's (`amiga/view.c`, layers and Amiga fonts) is not compiled; nor are `main.c`, `ruler.c` and the work packages' windows |

### What works, and what does not yet

Works (seen in the testbed): every tab of the Amiga (File, Home, Insert, Draw, Design, Layout, References,
Mailings, Review, View, and the contextual ones as their state allows), its groups in the Amiga's order with
their captions and icons, groups collapsing into one button when the window is narrow (the Amiga's own
fitting), the Quick Access Toolbar, the commands the Mac has (Bold and the rest, typing into the document
with the ribbon above it), the File page at 512 by 342 (New, Open, Save, Save As, Export PDF; Info; back
with Esc), and the status bar (the page, the language, the view buttons, the zoom slider). A screen under 400 lines (the
compact Mac's 342) starts with the ribbon collapsed to its tabs; a click on a tab shows its tools again,
as a double-click does on the Amiga. Of the ribbon's 423 commands (Quill's tabs and the packages', each once, as `ribbon.c`'s `ribbon_commands` counts them; the log says so at start), 71 are on the Mac; the others are drawn dimmed. The menus stay and keep every command the ribbon has on the
Mac (Style › All Caps, Clear Formatting and Format › Font Colour, Highlight are new, for the ribbon's).

**Second round (same night).** The drop-down lists open under their tool and stay (Escape or a click
elsewhere sends them away; the click also counts, as on the Amiga). They had closed at once because the font
list drew each family's name with Quill's TrueType rasteriser, loading every family's font (seconds and
megabytes on a 68k): on the Mac the names are drawn in the nearest screen font by QuickDraw (`qm_preview`,
for ribbon.c only, through the shim header). Riptide placed the list window centred, ignoring the place set
just before opening it; it keeps it now. The File page shows at 640 by 480, eight bits (it had failed only
because the lists had left Octavo busy). The Amiga files' text is Latin-1 as on the Amiga (their `tr()`
gives the table's Latin-1, `mac/qm_lang.c` `qm_tr_l1`) and becomes Mac Roman where it is drawn: "Résumé" is
right. When only states change (Bold where the caret is, Undo after the first key), ribbon.c draws again only
the tools whose look changed, not the whole ribbon (the first key: 204 ms before, 45 ms now; this is in the
shared file and helps the Amiga too). New commands, each also in a menu: Change Case (five forms), Sort,
Line Break, 125%: 73 of the 423 are on the Mac.

| ![The font list](mac-ribbon-tree-fontlist.png) The font list, names in the Mac's fonts | ![The size list](mac-ribbon-tree-sizelist.png) The size list |
|---|---|
| ![The File page at 640 by 480](mac-ribbon-tree-backstage.png) The File page, eight bits | |

Missing: the commands of the Amiga that the Mac has no code for yet are on the ribbon but dimmed (the
work packages' windows: mail merge, citations, the equation editor, shapes, SmartArt ...); the navigation
pane (its class compiles, but it reads the Amiga view's attributes, which Octavo's view lacks); the word
count in the status bar (shown as "..."); the Tell Me box and customizing the ribbon; right-button menus
(the Mac has one button); KeyTips are compiled but not tried on a Mac keyboard. Interface text with
Latin-1 characters outside Mac Roman's common set (the Symbol list's ©, the degree sign) is drawn as Mac
Roman bytes in English: the Amiga's tables are Latin-1, and only translated words go through Octavo's
conversion.

### Memory and CPU: what was changed

- **Pages kept packed.** The view kept the last three pages drawn as RGB, 1.4 MB each at 72 dpi (US
  Letter): 4.2 MB. Each is now kept run-length packed by rows (`core/rowpack.c`, tested natively on
  wordcraft-long.docx's pages: every row back as drawn; wordcraft-long.docx's first page, drawn smooth as the PDF has it: 281 KB of 1,420 KB), unpacked a row at a time as it is copied
  to the screen, and an edit's changed rows are found by comparing packed rows.
- **The layout's boxes at their size.** A long document's layout is mostly its boxes (a word or a space
  each, with its format: 98 bytes on the 68k), in arrays that grew by doubling from 64. Each paragraph's are
  trimmed to their size once laid out (`core/layout.c`): wordcraft-long.docx (158 pages, 155,632 boxes)
  from 24.4 to 16.0 MB natively.
- **A smaller FontSet.** Room for 48 faces on the Mac instead of 256 (`core/fonts.h` `MAXFACES`, set in
  Makefile.mac): 45 KB instead of 230.
- **Less code in the heap.** A classic Mac application's code is loaded into its heap. The core's files
  that do not run while typing, drawing or scrolling (the file formats, charts, equations, citations ...)
  are compiled for size (`-Os`), the rest for speed: the code from 2.36 to 2.04 MB.
- **Idle.** The page view's timer ran ten times a second for ever; it now runs so only while there is
  something to draw soon (typing, a drag, rulers to catch up) and otherwise at the caret's blink, changing
  rate by being taken off and put back. Octavo's loop asked for Apple events with a second WaitNextEvent
  each time round (a turn given to every other program); it now asks EventAvail first. Riptide's loop
  sleeps up to two seconds with nothing due (it slept half a second at most).
- **Lazy already.** The spelling list is read at the first Spelling…, hyphenation and the thesaurus are
  not loaded at all, the ribbon's list window is made when a list is first opened, dialogs are made when
  shown; the languages' words are made only for a language other than English.
- **SIZE.** 8 MB preferred and 6 MB minimum (12 MB both before), written by `mac/mksize.py` from Makefile.mac's `SIZE_PREF` and `SIZE_MIN`: 2.8 MB are in use at launch and 4.6 MB with charts.docx shown, which leaves 3.5 MB free of 8 MB in the testbed. A long document needs more (Get Info): wordcraft-long.docx about 20 MB.

### Measured

In the Aurora68 testbed (QEMU, the solo board, System 7.1, a 68040 through Emu68, 640 by 480 at eight
bits, 32 MB), one session per build, the figures written by Octavo itself into the log
(`mac/qm_measure.c`: the Memory Manager's FreeMem and the heap's size; Microseconds around every
WaitNextEvent; around each key's handling and each refresh of the window). *Before* is `mac-port` (the
tool strip) built with the same measuring code; the ribbon tree before its optimisations is the first
session; *after* is the ribbon tree as committed. The testbed's 68040 is emulated, so the times are for
comparing builds, not what a real Mac takes.

| | Before: `mac-port` (tool strip) | Ribbon tree, first build | Ribbon tree, after |
|---|---|---|---|
| Code (`.text`, in the heap) | 2.19 MB | 2.36 MB | 2.04 MB |
| Partition (SIZE) | 12 MB | 12 MB | 8 MB (6 MB minimum) |
| Heap in use at launch, untitled | 2,974 KB | 3,266 KB | 2,759 KB (second round 2,754 KB) |
| Heap in use, charts.docx shown (3 pages, charts) | 4,869 KB | — | 4,577 KB (3,287 KB with every page packed) |
| wordcraft-long.docx (158 pages) | out of memory (7.9 MB malloc'd, nothing shown); the next document then "couldn't be read" | out of memory | out of memory in 12 MB (needs about 20 MB); the next document then opens (4.5 MB in use) |
| Idle, untitled: wake-ups a second | 28–30 | 25–32 | 7.5–7.9 (the caret's blink, every 8 ticks in this System; nothing else) |
| Idle, charts.docx: wake-ups a second; busy | 29–32; 3.0–3.5 % | — | 7.4–7.8; 3.1–3.6 % |
| A key, handled (charts.docx) | 1.3–4.0 ms (the first 19 ms) | 2.7–6 ms | 1.6–5.7 ms (the first 204 ms); second round 1.7–3.7 ms (the first 45 ms, Cmd-B 18 ms) |
| The window brought up to date after it | 283–393 ms (page drawn in 14–20 ticks) | — | 297–555 ms (16–29 ticks); 474–592 ms with every page packed; second round 292–403 ms (15–23 ticks) |
| The ribbon drawn whole (632 by 103) | — | — | 92–293 ms; collapsed (632 by 25) 45–150 ms; the status bar 14–42 ms |

Idle CPU fell by three quarters in wake-ups but not in time: what is left is the caret's blink, which the
baseline did too, and each blink copies its column again. Every page packed saves 1.3 MB more with
charts.docx but made each redraw after typing a quarter of a second slower in the testbed; the page being
typed in is therefore kept as drawn. The first key after opening a document costs 0.2 s in the ribbon tree
(the ribbon's state changes: Undo becomes usable, and the ribbon is drawn again whole).

Natively (the core alone, a PC), for the long document: read 9 ms, laid out 17 ms (+16.0 MB, 24.4 before
the trimming), its first page drawn 5 ms; a key typed in its fourth paragraph and the layout brought up to
date 0.9 ms (only that paragraph is laid out afresh: the layout keeps every other one), its page drawn
again 3 ms.

### Screenshots

| | |
|---|---|
| ![Home, 640 x 480](mac-ribbon-tree-640.png) Home, charts.docx (the font field clicked) | ![Insert](mac-ribbon-tree-insert.png) Insert |
| ![Layout](mac-ribbon-tree-layout.png) Layout | ![References](mac-ribbon-tree-references.png) References |
| ![Review](mac-ribbon-tree-review.png) Review | ![View](mac-ribbon-tree-view.png) View |
| ![Draw](mac-ribbon-tree-draw.png) Draw (dimmed: WP20's commands) | ![Mailings](mac-ribbon-tree-mailings.png) Mailings (from wp_mailings.c's tables, dimmed) |
| ![Design](mac-ribbon-tree-design.png) Design | ![Collapsed to its tabs](mac-ribbon-tree-640-collapsed.png) Collapsed to its tabs (a tab double-clicked) |
| ![512 x 342, collapsed](mac-ribbon-tree-512.png) 512 by 342, one bit: it starts collapsed | ![512 x 342, a tab open](mac-ribbon-tree-512-open.png) Home clicked: its groups as buttons |
| ![512 x 342, Insert](mac-ribbon-tree-512-insert.png) Insert at 512 | ![The File page at 512](mac-ribbon-tree-512-backstage.png) The File page ("Résumé" shows the Latin-1 gap) |

### Not done

- **A long document needs more memory than the layout leaves.** wordcraft-long.docx (158 pages) needs
  about 15 MB for its layout on the 68k even trimmed: its boxes each carry a copy of their format (78 bytes).
  Keeping a format index in the box instead, or dropping the boxes of paragraphs far from the pages shown
  and laying them out again when drawn, would bring it to a few MB; both touch every reader of `Box.f`
  (about 180 places, 90 of them in the Amiga's view.c), so they were left for a branch of their own.
- **Typing draws the whole page again** in memory once the keys pause, then copies only the rows that
  changed: the redraw after a key is the baseline's (292–403 against 283–393 ms), not below it. Drawing only
  the band of the changed lines needs `raster_page` to work on a band of rows (its clip and the CTM's offset),
  the QuickDraw text pass to draw and merge only the runs in it, and its caret map (QtWords) to keep the other
  runs' places from the page drawn before; not done.
- The ribbon redraws whole when its state changes (the Amiga's design); the times are in the log
  (`draw 632x98 ...`).
- The navigation pane, the word count, Tell Me, customizing, right-button menus; the dimmed commands.

## Building

    make -f Makefile.mac                     build/mac/Octavo.bin (MacBinary; Quill.bin before the rename)
    make -f Makefile.mac AURORA68=1 B=build/mac-a68    the log also goes to Aurora68's kernel log

The experimental ribbon tree (branch `octavo-ribbon`) needs Riptide's `octavo-ribbon` branch:

    make -f Makefile.mac RIPTIDE=/path/to/riptide-octavo-ribbon

Needs the m68k ELF toolchain (`gcc-m68k-linux-gnu`, `binutils-m68k-linux-gnu`), Python 3 and Riptide's
tree beside Quill's (`RIPTIDE ?= ../riptide`, branch `mac-port`). The icons are made by `mac/mkfinder.py`
(needs PIL) and kept in `mac/rsrc/`, so the build itself does not need PIL.

## Installing

Copy `build/mac/Octavo.bin` to the Mac (`hcopy -m` onto an HFS image, or any MacBinary decoder), and beside
it a folder named **Fonts** holding everything in `fonts/` (the `.ttf` files and `fonts.list`). Octavo
looks for the Fonts (and dict) folder in the folder its own file is in, wherever that is and however it was
started. Its memory size is 12 MB: a page drawn at 100% is
1.4 MB of pixels and three are kept, a .docx is unzipped in memory and a font in use is loaded whole.

Documents: Octavo opens anything its core reads (.docx, .doc, .odt, .rtf, .html, .md, .txt), whatever its
Mac type, since Word documents from other computers arrive typed `????`. It saves by the name's ending, a
.docx when the name has none, and gives the file the Mac type for its format and Octavo's creator
`QUIL`: `TEXT` for text formats, `DOCX` for .docx/.docm/.dotx, `ODT ` for .odt, `PDF ` (Acrobat's
creator) for exported PDFs. Its bundle tells the Finder that Octavo opens `DOCX` and `TEXT` files and gives
them Octavo's document icons (the Amiga's icon pictures); documents double-clicked or dropped on Octavo in the Finder are opened (the
first one: one window, one document).

## Using it

The window is a document window with the page number box ("Page 1 of 2") beside the horizontal scroll
bar, as Word 5 for the Mac had it. Click to place the insertion point, drag or Shift-click to select,
double-click a word. Arrow keys move, Option-Left/Right by words, Option-Up/Down to the line's ends, Shift
extends the selection; Home and End go to the document's ends, Page Up and Page Down scroll; Delete and
forward delete, Option with them a word; Return a new paragraph, Shift-Return a line break.

| Menu | Commands (Command keys) |
|---|---|
| Apple | About Octavo… |
| File | New ⌘N, Open… ⌘O, Close ⌘W, Save ⌘S, Save As…, Revert, Export PDF… ⌘D, Quit ⌘Q |
| Edit | Undo ⌘Z, Cut ⌘X, Copy ⌘C, Paste ⌘V, Clear, Redo ⌘Y, Select All ⌘A, Find… ⌘F, Find Again ⌘G, Replace… |
| Font | Quill's families (Caladea, Carlito, Liberation Mono, Sans and Serif), the caret's checked |
| Size | 9 10 12 14 18 24 36, Grow Font ⌘>, Shrink Font ⌘< |
| Style | Plain Text ⌘T, Bold ⌘B, Italic ⌘I, Underline ⌘U, Strikethrough, Superscript ⌘+, Subscript ⌘- |
| Format | Font…, Paragraph Style ▸ (Normal, Title, Subtitle, Heading 1-3, Quote, List Paragraph), Align Left ⌘[, Centre ⌘E, Align Right ⌘], Justify ⌘J, Bullets, Numbering, Decrease Indent ⌘K, Increase Indent ⌘M, Single ⌘1, 1.5 Lines ⌘5, Double ⌘2 |
| Insert | Table…, Picture…, Breaks ▸ (Page Break, Column Break, Section Break (Next Page), Section Break (Continuous)), Date, Page Number, Symbol ▸ (22 characters Mac Roman has) |
| Layout | Page Setup…, Portrait, Landscape, Columns ▸ (One, Two, Three), Line Numbers ▸ (None, Restart Each Page, Restart Each Section, Continuous), Paragraph…, Table ▸ (Insert Rows Above, Below, Columns to the Left, Right, Delete Rows, Columns, Table, Merge Cells, Split Cells) |
| References | Table of Contents, Footnote…, Endnote…, Next Footnote, Caption…, Bookmark…, Cross-reference… |
| Review | Spelling…, Word Count, New Comment…, Previous Comment, Next Comment, Delete Comment, Track Changes, Previous Change, Next Change, Accept Change, Reject Change, Accept All Changes, Reject All Changes |
| View | Ribbon, Tool Strip ▸ (Home, Insert, Layout, References, Review), Ruler, Fit Page Width ⌘0, 50%, 75%, 100%, 150%, 200% |

The keys are the Amiga's where the Mac does not reserve them (Guidelines p. 128). Changed: Save As has no
key (the Amiga's Amiga-A is Select All on the Mac, whose Amiga-L is gone), Page Break has none (Amiga-P is
Print), Table (Amiga-T) is not on the Mac, whose ⌘T is Plain Text as the Guidelines' Style menu has it.
A Style, Font or Size command applies to the selection, or to the word the caret is in, as Word does;
between words it sets what is typed next. Cut and Copy put the text on the Clipboard for other programs
(Mac Roman, returns between paragraphs) and keep Octavo's own copy with its formatting for Paste in Octavo.
Closing the window asks to save changes (Save, Don't Save, Cancel) and leaves Octavo running; New and Open
bring the window back. The interface is in English, German, French, Spanish or Italian, from the System's
region ('vers' 1), with the words Quill's Amiga version uses.

## Octavo and the Guidelines: Cornerstone's classic Mac chapter, point by point

Checked on 18 November 2026 against Cornerstone's classic Mac chapter and Apple's *Macintosh Human
Interface Guidelines* (1992), whose page numbers these are. **Conforms**, **changed** (in this branch),
or **left** (and why).

| Point | Octavo |
|---|---|
| Menus and their order (pp. 49-130) | **Changed, and over Cornerstone's count; folded on a narrow screen** (23 November: on 512 pixels Size goes into Font, References into Insert, Layout into Format, "The compact Mac's screen" above). Apple, File, Edit, then Octavo's Font, Size, Style, Format, Insert, Layout, References, Review and View, then the system's Help and Application menus. Nine menus of Octavo's own, where Cornerstone asks for four or five on a small screen (HIG p. 80): agreed with the user, to follow the Amiga's ribbon tabs (Insert, Layout, References and Review between Format and View). Whether every title fits a 640 by 480 screen is checked in the testbed below; a longer translation may not fit. |
| Apple menu (p. 98) | **Conforms.** About Octavo… first, a line, the desk accessories (Riptide's Mac MUI). |
| File (pp. 99-109) | **Changed:** Revert added, dimmed until a document read from a file is changed; it asks first, Cancel the default (it loses changes). Page Setup and Print are left out rather than shown dimmed for ever, as Cornerstone asks: Octavo does not print yet. Export PDF… sits after Save As. |
| Edit (pp. 109-117) | **Conforms**, with one difference left: Undo, Cut, Copy, Paste, Clear, then Octavo's Redo and Select All after a line, where Riptide's layer puts a program's own Edit items. Show Clipboard is not offered. |
| Preferences | **Conforms by having none to set in a dialog:** the only settings (Ribbon, Ruler) are View's toggles, remembered between sessions (p. 37). A Preferences… item comes with a first dialog of settings. |
| Item names (pp. 58, 67) | **Conforms.** Title case, "…" where a dialog follows (Riptide turns "..." into the ellipsis character). |
| Hierarchical menus (p. 79) | **Changed:** the attribute lists are submenus: Format › Paragraph Style, Insert › Breaks and Symbol, Layout › Columns, Line Numbers and Table, View › Tool Strip (Riptide's layer gained submenus for them). |
| Command keys (p. 128) | **Conforms.** N O W S Q Z X C V A keep Apple's meaning; P and period are unused (no printing, no long operation to stop yet). T B I U are the Style menu's, F and G Find and Find Again as Apple suggests. The Amiga's other keys stay where the Mac leaves them free: D Export PDF, Y Redo, E [ ] J alignment, 1 5 2 spacing, < > font size, + - super- and subscript, 0 Fit Page Width, K and M the indents. The new menus' commands have no keys: equivalents only for the most used commands. |
| Dimming (pp. 54, 60, 63) | **Conforms.** Undo, Cut, Copy, Clear and Paste follow what can be done (`mm_edit_state`); a paragraph style the document lacks is dimmed, not removed; Revert is dimmed until there is something to revert. Every item's dimming comes from `qm_state()`. |
| Toggles (p. 75) | **Conforms.** Style, alignment, spacing, lists, zoom, Ribbon and Ruler show check marks, from the same answer as the strip's buttons. |
| No ribbon; a tool strip; pop-up menus (pp. 82-91, 96-97) | **Changed:** the strip and rulers above. The strip is inside the document window under the title bar, nowhere near the scroll bars or the close box; its lists are pop-up menus with a shadowed box and a triangle; it invents no command. |
| Windows (pp. 131-174) | **Conforms** for the window: title bar, close box, zoom box, size box, scroll bars inactive when the page fits, staggered placement (Riptide). **Left:** one window, one document (opening another asks to save the first). |
| Status box (p. 161) | **Conforms.** "Page 1 of 2" at the left end of the horizontal scroll bar, as Word 5 had it, following the page at the top of the window. |
| Dialogs and button order (pp. 185, 196, 204-206) | **Changed: Octavo has dialogs now** (Insert Table, Page Setup, Paragraph, Font, Find and Replace, a note's or comment's text, Caption, Bookmark, Cross-reference, Spelling), built with the Dialog Manager (`mac/qm_dialog.c`): no title bar, the prompt at the top in the system font, the action at the bottom right with Cancel to its left, the default ringed and answering Return and Enter, Cancel answering Escape and Command-period, check boxes for on and off and radio buttons for two to five choices, Tab between fields. Open, Save As, Export PDF and Insert Picture are Standard File. **Left:** Find and Replace is modal (it closes to show what it found), not the modeless dialog Cornerstone and the Guidelines (p. 178) would have: Riptide's event loop does not pass events to modeless dialogs yet. |
| Alerts (pp. 193-196) | **Changed:** Octavo's alerts have their level now (Riptide's `mac_alert` with its icon): a stop for what can't be done (a file that can't be read or saved, a picture Octavo can't read, numbers a dialog can't take), a note for information (nothing found, replacements made, the word count, a comment's text, the spelling check done), a caution before Revert. Save, Don't Save, Cancel still goes through MUI's requester (a caution). |
| Progress (pp. 185, 206) | **Left.** Opening a long document shows the watch, not a movable modal dialog with a Stop button; nothing in Octavo can be stopped part way yet. |
| Fonts (pp. 23, 60, 207) | **Changed:** the strip and rulers use Geneva 9, as Cornerstone asks for palettes. Menus and alerts are the system font; the page box uses Riptide's Geneva 9 or 10 (10 on screens taller than 600 lines). Since 23 November the page's text is the System's own screen fonts (Times, Helvetica, Courier, bitmap sizes favoured). **Left:** the Font menu does not show families in their own faces: they are Quill's TrueType fonts (what the layout measures with), which the Font Manager does not have. |
| Colour and one bit (pp. 258, 263-264) | **Conforms.** Everything was laid out on one bit first: the strip is white with black frames and inverted "on" buttons, the ruler's margins a light pattern. Colour from four bits up is only the icons' accents and light greys; the selection takes the Color control panel's highlight colour. |
| Balloon Help (pp. 316-325) | **Left.** No `hmnu` or `hrct` balloons yet. Quill's help texts are written for the Amiga's ribbon bubbles (`amiga/ribbon.c`), and Riptide's layer shows its own bubbles only for MUI objects with `MUIA_ShortHelp`, which the strip, drawn by the view, is not. Generating `hmnu` resources from the strings table is the way. |
| Keyboard (pp. 277, 282-283) | **Conforms** for moving and selecting: arrows, Option-arrows by words, Shift extending, Home, End, Page Up and Page Down. **Left:** Option-Up and Option-Down go to the line's ends, the Amiga's habit, where many Mac programs used Command-Left and Command-Right; the Guidelines leave Command-arrows to keyboard scripts. |
| Strings and localisation (pp. 15-24) | **Conforms:** every word through `tr()` from `tools/lang/`, five languages, the System's region choosing, Mac Roman throughout. **Changed:** the Style menu was "Formatvorlage" (a paragraph style) in German; it is now "Stil", as the Mac's German system calls it (`menu:Style` in `tools/lang/mac.py`). |

## Tested

In the Aurora68 testbed (QEMU, the solo board, a copy of the System 7.1 image with Octavo (then still called Quill) and its fonts in
Startup Items and `PiDepth N` setting the screen to N bits; `mac.screen=1024x768` with its declaration
ROM), at 1, 8 and 32 bits, scripted through QEMU's monitor (keys and mouse):

1. Octavo starts with an empty Untitled document.
2. ⌘O, Standard File, ⌘↑ twice to the disk, "word" selects word.docx (a copy of tests/data/word.docx),
   Return: the document opens, the window is titled word.docx, "Page 1 of 2".
3. "Hello from the Mac. " typed at the start of the heading; Shift-Right five times selects "The Q"; ⌘B.
4. ⌘S saves (no dialog: the document has a name); ⌘W closes the window without asking (nothing unsaved);
   ⌘O and word.docx again: the text and the bold are there.
5. The saved file, copied off the disk image, read by the core on the host: `Hello from the Mac. ` and a
   bold run `The Q` in word/document.xml, the rest of the document as before.

Also: Quill's core with `mac/libc.c` in place of the system's C library, built for the host and for the
68020 with `-msoft-float` and run under qemu-m68k, draws word.docx's first page identical, pixel for
pixel, to the native build. The native tests (2556) pass; `tools/langcheck.py` finds nothing missing.

### The strip and the rulers (18 November 2026)

In the same testbed, at 1, 4, 8 and 32 bits on a 1024 by 768 screen and at 8 bits on a 640 by 480 one
(the default declaration ROM is that size), with tests/data/word.docx, the mouse driven through QEMU's
monitor:

1. Opened, the strip shows the caret's style, font and size (Title, Calibri, 26) and the ruler the page:
   inches on this US Letter page, the indents on the text's edges.
2. "Before" double-clicked, then the strip's Bold and Centre: the word bold, the paragraph centred, both
   buttons on, the menus' check marks the same.
3. "Step two": Bullets, then Increase Indent: a bullet, one level in; Bullets shown on.
4. The font pop-up held open (the Font menu's families), Liberation Serif chosen for "heading"; another
   run, Liberation Sans for "Plain": the box and the text follow (but see below).
5. The justified paragraph's left indent dragged half an inch (the dotted line follows): a hanging
   indent. A tab stop clicked at 4 inches on "Plain text…", then dragged to 5½: "after a tab" moves there.
   In a table, the middle column edge dragged half an inch: the columns follow.
6. ⌘S, then the disk image read on the host: in word/document.xml the justified paragraph has
   `<w:ind w:left="720" w:hanging="720"/>`, "Plain text…" `<w:tab w:val="left" w:pos="7920"/>`, "Before
   the break" `<w:jc w:val="center"/>` with "Before" `<w:b/>`, "Step two" `<w:ilvl w:val="1"/>`, and the run
   "heading" `<w:rFonts w:ascii="Liberation Serif" …/>`.
7. Format › Paragraph Style (a submenu) chose Heading 2; View › Ruler and View › Ribbon hid them, and
   "Quill Preferences" (type `pref`; "Octavo Preferences" since the rename, which reads and replaces an old one) held `ribbon 0`, `ruler 0`; a disk with `ruler 0` in it started
   without the ruler and with View › Ruler unchecked.
8. File › Revert after typing: the caution alert, Cancel the default; Revert reads the file again.
9. On the 640 by 480 screen the window is 632 wide and the whole strip fits; narrowed to about 390
   pixels with the size box, the alignment, list and indent groups go and the pop-ups and B I U stay.

![1 bit: "Before" bold and the paragraph centred from the strip](mac-ribbon-1bit-bold.png)

The native tests (2600, 44 of them the ruler's in tests/test_wpmac.c) pass, both Mac builds have no
warnings, and `tools/langcheck.py` finds nothing missing for `amiga/` or `mac/`.

### The switchable strips (19 November 2026)

At 8 bits on a 1024 by 768 screen, on a copy of word.docx that already held a table and a picture from
an earlier run, driven through QEMU's monitor:

1. The switcher pop-up (and View › Tool Strip, a submenu with the current strip checked) chose Insert,
   References, Layout and Review in turn.
2. Insert: Picture… (Standard File, disc.png) put the picture at the start, drawn at once; Table… (3
   columns, 2 rows, Return for Insert) put a table after "Before the break", the caret in its first
   cell and its column edges on the ruler.
3. References: Table of Contents put a table of contents at the caret (Contents, First heading 1,
   Lists 1).
4. Layout: Page Setup… showed US Letter, portrait, margins 1, 1, 1.25, 1.25 inches; Landscape and OK
   turned the page.
5. Edit › Find… (⌘F): "Quill" replaced with "Mac" by Replace All; a note said one replacement.
6. Review: New Comment… on a word ("Checkthis": the script lost the space); Track Changes on, and
   "Tracked " typed at the start of a paragraph showed underlined in the tracked colour.
7. ⌘S, then on the host, in word/document.xml: three tables, two `w:drawing`s (word/media/image1.png and
   image2.png), `<w:pgSz w:w="15840" w:h="12240" w:orient="landscape"/>`, the TOC field, "The Mac
   Test", `<w:ins w:author="Quill" …>` round "Tracked " (the System had no owner name; now "Octavo"),
   a comment range, and word/comments.xml with the comment.

Spelling ran in this boot without its word list on the disk and gave the stop alert that says so.
In a second boot, at 1 bit on a 640 by 480 screen with the word list in a dict folder beside Octavo
and an old "Quill Preferences" in the Preferences folder: Octavo started with the old settings and
wrote them as "Octavo Preferences" (type `pref`, creator `OCTV`), deleting the old file. Spelling on
word.docx found nothing; with "Speling" typed in a heading it asked about it with Spelling, Spieling
and Spewing, and Change put Spelling in. The Apple menu read About Octavo…

## Not done yet

- **Printing** is left out. Octavo draws pages for PDF; Export PDF writes one. Printing needs the Printing
  Manager's QuickDraw (PrOpen, PrOpenDoc, a page at the printer's resolution through the same CopyBits
  path), not hard, not done.
- **One window, one document.** The Amiga has several windows; the Mac version opens one document at a
  time (opening another asks to save the first).
- **About a third of the Amiga's ribbon** on the five strips' tabs (81 of 268; the table under "Switchable
  strips"). Not on the Mac yet: the styles gallery and the style dialogs (Create, Modify, Manage), headers
  and footers to edit, text boxes, shapes, charts, SmartArt and equations to insert or edit (the core
  keeps and draws them), citations, the index and tables of figures and authorities, mail merge, the
  thesaurus, grammar, compare and combine, restricting editing, the Design and View tabs beyond zoom, and
  the table tools past rows, columns, merge and split (properties, borders, shading, sort, formulas).
  The core reads and writes all of them, so a document keeps them.
- **Dialogs are modal.** Find and Replace closes to show what it found (Find Again repeats it); a
  modeless one needs Riptide's event loop to pass events to dialogs. Notes and comments are typed in a
  dialog (one paragraph), not edited in place as on the Amiga; comments are shown one at a time in a note,
  not drawn as balloons in the margin.
- **Spelling** checks British English only (the word list from a dict folder beside Octavo), without
  grammar; Ignore All lasts for the session (no user dictionary is saved). **Hyphenation** is not offered:
  the core has it, but the Mac does not load its patterns.
- **Tracked changes** cover typing, Delete, Cut, Clear and pasted text, and formatting; what Octavo's own
  commands put in (a table, a field, a picture) is not marked as a tracked insertion.
- **Page Setup** sets the whole document's page; sections with their own page setup keep theirs.
- **The ruler's unit** follows the Amiga's rule (inches only on an upright US Letter page): a landscape
  Letter page gets centimetres. Page Setup's margins follow the same unit.
- **The strip and rulers**: no Balloon Help for them; the vertical ruler does not drag the margins (nor
  does the Amiga's); no Show ¶ (the page view has no marks to show); the units follow the page size, as
  there is no unit preference yet. The Amiga's ruler (amiga/ruler.c) still does its own sums: switching it
  to core/ruler.c needs the vbcc build to check, which this machine lacks.
- **A font chosen but not seen**: in the testbed, Liberation Serif chosen for the bold heading's word was
  in the document (the saved file says so, and the host draws it serif), but the Mac's page kept drawing
  the sans face; Liberation Sans on body text showed at once. Not explained yet; a font that fails to load
  on the Mac (memory, with three pages kept) falls back silently, so that is the first suspect. (With the
  Mac's fonts drawing the text since 23 November the face shown follows the document's family name; the
  widths the layout uses still come from the TrueType face that loaded.)
- **Selection** is drawn line by line from margin to margin between its ends (not hugging each line's
  text), and the caret's line is found by the core's layout, so tables and columns select roughly.
- **Speed** on a real 68030 is unmeasured; the testbed's figures are above. Drawing only a band of the
  page (the core draws whole pages) would make typing on slow machines cheaper.
- **Apple events**: since 23 November Octavo takes Open Documents, Print Documents (it opens them) and Quit,
  at launch and while it runs; the first document only (one window). No other events (no scripting).
- **Text in the Mac's fonts**: what QuickDraw can't draw as the document says stays smooth (characters
  Mac Roman lacks, small capitals, scaled, embossed or engraved text, text in turned frames). Text over a
  picture or shape that lies in front of it is drawn on top (the core's order is lost for text). A text
  box's text is not clipped to its box. Only Mac Roman; System 7.1's fonts. The sizes are chosen per page's
  sample and kept for the session; a document whose first page has little of some kind of text may get a
  less good size for it. Not looked at on 68030 hardware.
- **Paths**: full paths ("Disk:Folder:file"), as the core opens files by name: two mounted volumes of one
  name reach the first, and a path over 255 characters can't be opened (an FSSpec all the way through
  the core's file layer would end both).
- The Font menu draws family names in the system font, not in themselves; the Style menu does not show
  its items in their styles.
