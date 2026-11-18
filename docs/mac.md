# Quill for the classic Mac OS

Quill runs on a Macintosh with a 68030 or 68040 and System 7: it opens a Word document, shows its pages
as Quill draws them for PDF, lets you type and format, and saves .docx again. This is the story of the
port, how to build and install it, what it does and what it does not do yet. Started 18 November 2026 on
branch `mac-port`.

![Quill on a 1-bit screen, the Style menu down](mac-1bit.png)

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
| `mac/qm_view.c` | the page view, a MUI Area subclass: drawing, scrolling, the caret and selection, keys and clicks |
| `mac/qm_lang.c` | `tr()`: Quill's interface words (`amiga/lang_table.h`) in Mac Roman, the language from the System's region |
| `mac/qm_text.c` | Mac Roman to and from UTF-8 and Latin-1 |
| `mac/libc.c`, `mac/include/` | the C library the core needs (allocator, stdio over the File Manager, time, strings, sorting) |
| `mac/qm_mac.c` | the File Manager by path, file types and creator, FlushVol, the clock, the log, the System's region |
| `mac/qm_thunks.S` | the class dispatcher's register entry and 32-bit QuickDraw's offscreen calls |
| `mac/traps.txt` | the Toolbox calls Quill adds to Riptide's list (CopyBits, Draw1Control, EventAvail, resources) |
| `mac/mkfinder.py`, `mac/rsrc/` | the Finder's resources: bundle, file references, icons in every size and depth, 'vers' |

### Pages on the screen, 1 to 32 bits

The view never draws text through the Toolbox. For each page it shows it asks the core for the page as
pixels (`pdf_page_rgb`, core/raster.c: exactly what Export PDF writes, drawn with Quill's own TrueType
rasteriser in fixed point), at the zoom's resolution: 72 dots per inch at 100%, the Mac screen's own, so
a 12-point letter is 12 pixels high. It keeps the last three pages drawn. Everything that reaches the
window goes through one routine, `blit()`: a rectangle of one page converted a band of 32 rows at a time
into an offscreen 32-bit GWorld, with the selection and the caret mixed in, and copied with CopyBits.
CopyBits fits the colours to the screen, whatever its depth: on 1-bit and indexed screens with
`ditherCopy`, on 16 and 32 bits as they are. On a 1-bit screen each level is stretched first (72 and
darker black, 200 and lighter white), because smoothed text edges dithered as they are come out speckled;
pictures keep their dithered shades.

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

### Speed

Drawing one US Letter page at 72 dpi in the testbed (QEMU emulating the Pi 3 that runs Emu68, many times
slower than real hardware): an empty page 82 ticks (1.4 s), tests/data/word.docx's first page 200 to 290
ticks (3.3 to 4.8 s). On the host the same page takes 2 ms. The log (`make -f Makefile.mac AURORA68=1`)
prints each page's time. A 25 MHz 68030 should be around a second a page; that is why typing is drawn
once the keys stop rather than per key, and why only changed rows are copied.

## Building

    make -f Makefile.mac                     build/mac/Quill.bin (MacBinary)
    make -f Makefile.mac AURORA68=1 B=build/mac-a68    the log also goes to Aurora68's kernel log

Needs the m68k ELF toolchain (`gcc-m68k-linux-gnu`, `binutils-m68k-linux-gnu`), Python 3 and Riptide's
tree beside Quill's (`RIPTIDE ?= ../riptide`, branch `mac-port`). The icons are made by `mac/mkfinder.py`
(needs PIL) and kept in `mac/rsrc/`, so the build itself does not need PIL.

## Installing

Copy `build/mac/Quill.bin` to the Mac (`hcopy -m` onto an HFS image, or any MacBinary decoder), and beside
it a folder named **Fonts** holding everything in `fonts/` (the `.ttf` files and `fonts.list`). Quill
looks for `:Fonts:` in its own folder at launch. Its memory size is 12 MB: a page drawn at 100% is
1.4 MB of pixels and three are kept, a .docx is unzipped in memory and a font in use is loaded whole.

Documents: Quill opens anything its core reads (.docx, .doc, .odt, .rtf, .html, .md, .txt), whatever its
Mac type, since Word documents from other computers arrive typed `????`. It saves by the name's ending, a
.docx when the name has none, and gives the file the Mac type for its format and Quill's creator
`QUIL`: `TEXT` for text formats, `DOCX` for .docx/.docm/.dotx, `ODT ` for .odt, `PDF ` (Acrobat's
creator) for exported PDFs. Its bundle tells the Finder that Quill opens `DOCX` and `TEXT` files and gives
them Quill's document icons; documents double-clicked or dropped on Quill in the Finder are opened (the
first one: one window, one document).

## Using it

The window is a document window with the page number box ("Page 1 of 2") beside the horizontal scroll
bar, as Word 5 for the Mac had it. Click to place the insertion point, drag or Shift-click to select,
double-click a word. Arrow keys move, Option-Left/Right by words, Option-Up/Down to the line's ends, Shift
extends the selection; Home and End go to the document's ends, Page Up and Page Down scroll; Delete and
forward delete, Option with them a word; Return a new paragraph, Shift-Return a line break.

| Menu | Commands (Command keys) |
|---|---|
| Apple | About Quill… |
| File | New ⌘N, Open… ⌘O, Close ⌘W, Save ⌘S, Save As…, Export PDF… ⌘D, Quit ⌘Q |
| Edit | Undo ⌘Z, Cut ⌘X, Copy ⌘C, Paste ⌘V, Clear, Redo ⌘Y, Select All ⌘A |
| Font | Quill's families (Caladea, Carlito, Liberation Mono, Sans and Serif), the caret's checked |
| Size | 9 10 12 14 18 24 36, Grow Font ⌘>, Shrink Font ⌘< |
| Style | Plain Text ⌘T, Bold ⌘B, Italic ⌘I, Underline ⌘U, Strikethrough, Superscript ⌘+, Subscript ⌘- |
| Format | Align Left ⌘[, Centre ⌘E, Align Right ⌘], Justify ⌘J, Single ⌘1, 1.5 Lines ⌘5, Double ⌘2, Page Break |
| View | Fit Page Width ⌘0, 50%, 75%, 100%, 150%, 200% |

The keys are the Amiga's where the Mac does not reserve them (Guidelines p. 128). Changed: Save As has no
key (the Amiga's Amiga-A is Select All on the Mac, whose Amiga-L is gone), Page Break has none (Amiga-P is
Print), Table (Amiga-T) is not on the Mac, whose ⌘T is Plain Text as the Guidelines' Style menu has it.
A Style, Font or Size command applies to the selection, or to the word the caret is in, as Word does;
between words it sets what is typed next. Cut and Copy put the text on the Clipboard for other programs
(Mac Roman, returns between paragraphs) and keep Quill's own copy with its formatting for Paste in Quill.
Closing the window asks to save changes (Save, Don't Save, Cancel) and leaves Quill running; New and Open
bring the window back. The interface is in English, German, French, Spanish or Italian, from the System's
region ('vers' 1), with the words Quill's Amiga version uses.

## Tested

In the Aurora68 testbed (QEMU, the solo board, a copy of the System 7.1 image with Quill and its fonts in
Startup Items and `PiDepth N` setting the screen to N bits; `mac.screen=1024x768` with its declaration
ROM), at 1, 8 and 32 bits, scripted through QEMU's monitor (keys and mouse):

1. Quill starts with an empty Untitled document.
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

## Not done yet

- **Printing** is left out. Quill draws pages for PDF; Export PDF writes one. Printing needs the Printing
  Manager's QuickDraw (PrOpen, PrOpenDoc, a page at the printer's resolution through the same CopyBits
  path), not hard, not done.
- **One window, one document.** The Amiga has several windows; the Mac version opens one document at a
  time (opening another asks to save the first).
- **Only the commands in the menus above.** No ribbon, panes or dialogs: no Find and Replace, styles
  gallery, tables, pictures, comments, track changes, spelling, page setup or headers and footers to edit,
  though the core keeps all of them in a document it reads and writes them out again.
- **Selection** is drawn line by line from margin to margin between its ends (not hugging each line's
  text), and the caret's line is found by the core's layout, so tables and columns select roughly.
- **Speed** on a real 68030 is unmeasured; the testbed's figures are above. Drawing only a band of the
  page (the core draws whole pages) would make typing on slow machines cheaper.
- **Apple events**: Quill takes documents from the Finder by the Segment Loader's parameters (System 6
  style, which the System 7 Finder still sends to programs that do not declare high-level events); 'odoc'
  while running, and drag-and-drop onto a running Quill, are not handled.
- The Font menu draws family names in the system font, not in themselves; the Style menu does not show
  its items in their styles.
