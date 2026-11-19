# Bureau

Bureau is five programs for AmigaOS 3.x in one package, with one installer and the fonts and
dictionaries they share installed once:

| Program | What it is |
|---|---|
| **Quill** | word processor: reads and writes Word .docx, exports PDF |
| **Ledger** | spreadsheet: reads and writes Excel .xlsx, 516 worksheet functions, charts, pivot tables |
| **Easel** | picture editor: layers, masks, filters, type; PNG, JPEG, GIF, IFF ILBM, PSD, TIFF, WebP, AVIF...; EaselConvert for the Shell |
| **Folio** | PDF viewer and editor: comments, forms, page tools, signing |
| **Dispatch** | mail, calendar, contacts and tasks (IMAP, SMTP, POP3, CalDAV, CardDAV); DispatchConvert for the Shell |

Each program's own README.md, in its drawer, tells what it does and what is new.

## What it needs

A 68040 or 68060 (with its FPU), AmigaOS 3.x, MUI 3.8 or later (or AROS or MorphOS, which have
MUI built in). Dispatch also needs a TCP/IP stack and
AmiSSL 5 to reach mail servers; everything else in it works without them. The windows fit a 640x480
screen; the interface is in English, German, French, Spanish and Italian (Locale prefs).

## Installing

Unpack `Bureau-<version>.lha` anywhere (RAM: will do) and double-click **Install-Bureau** in the
Bureau drawer. It asks:

1. where Bureau goes (`Work:Bureau` unless Bureau is installed already, then where it is);
2. which programs to install (all five are ticked).

Then it puts:

| What | Where |
|---|---|
| the screen fonts (QuillSans, QuillSerif, QuillMono, QuillSansC, QuillSerifC and their styles) | `FONTS:` (only those missing there or older), so other programs can use them too |
| the TrueType fonts (Liberation, Carlito, Caladea) | `Bureau:TrueType` |
| the word list, thesaurus and hyphenation patterns | `Bureau:Dict` |
| each program chosen | `Bureau:Quill`, `Bureau:Ledger`, `Bureau:Easel`, `Bureau:Folio`, `Bureau:Dispatch`: the program, its README.md and example files, with icons |
| `Assign Bureau: <the drawer>` | `S:User-Startup` (between `;BEGIN Bureau` and `;END Bureau`), and assigned at once |

Installing again over an installed Bureau updates it: only the files Bureau ships are replaced, so
each program's settings, recent files, the words you added to the dictionary, caches and
Dispatch's mail (all kept in the program's own drawer) stay, and so does `TrueType/fonts.list` if
you have added fonts to it. A program you leave unticked is left as it is.

To remove Bureau: delete the Bureau drawer (copy Dispatch's `Mail` drawer somewhere safe first if
you want your mail), the `;BEGIN Bureau` ... `;END Bureau` lines in `S:User-Startup`, and, if no
other program uses them, the `Quill*.font` files and drawers in `FONTS:`.

## Where the programs look for their files

Each program looks for each kind of shared file in three places, and the first that is there wins:

| Kind | First | Second | Third |
|---|---|---|---|
| screen fonts | `PROGDIR:Fonts` | `Bureau:Fonts` | `FONTS:` |
| TrueType fonts | `PROGDIR:TrueType` | `Bureau:TrueType` | - |
| dictionaries | `PROGDIR:Dict` | `Bureau:Dict` | - |

`PROGDIR:` is the program's own drawer. So a program installed from its own single-app package,
with its Fonts, TrueType and Dict drawers beside it, works exactly as before, and a program
installed by Bureau needs nothing beside it. A missing `Bureau:` is looked for quietly (no "Please
insert volume Bureau" requester). What a program writes (settings, recent files, your words, font
caches) stays in its own drawer.

## Single-app packages

Each program is still released on its own too (`Quill-<version>.lha`, `Ledger-<version>.lha`,
`Easel-<version>.lha`, `Folio-<version>.lha`, `Dispatch-<version>.lha`), with its own installer and
its own copy of the fonts and dictionaries, and works alone. Bureau holds the same programs; it is
smaller than the five packages together because the shared files are in it once. A program's own
installer, run on an Amiga where Bureau is installed, offers to put the program in Bureau and
leaves out the drawers Bureau already has.

## Source code

`Bureau-<version>-src.lha` holds each program's C source (what each single-app package carries in
its Source drawer); each builds with vbcc, the NDK 3.2 and the MUI 3.8 developer headers as its
README says. The packages are made by `suite/package.sh` and `suite/source.sh` in Quill's
repository (see `suite/README.md` there).

## Licences

- **The programs:** GNU General Public License, version 2 (GPL-2.0-only); see `LICENSE`. Work by
  others in them keeps its own licence, all of which allow this: see each program's README.md
  ("Licences"), `Easel/Licenses.txt` and `Dispatch/Licenses.txt`.
- **TrueType fonts:** Liberation (Red Hat, Google), Carlito and Caladea, under the SIL Open Font
  License 1.1: `TrueType/LICENSE-fonts.txt`. The screen fonts are renderings of them (modified
  versions under the same licence, renamed QuillSans, QuillSerif, QuillMono, QuillSansC and
  QuillSerifC as the licence requires).
- **Word list:** SCOWL's British English list, `Dict/LICENSE-dict.txt`. **Thesaurus:** made from
  WordNet, `Dict/LICENSE-thesaurus.txt`. **Hyphenation patterns:** from hyph-utf8, each with a
  `.txt` beside it and `Dict/LICENSE-hyph.txt`.
