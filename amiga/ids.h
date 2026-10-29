/* ids.h - the commands of Quill's menus, ribbon and windows (main.c's input loop). Each is the value
   a control hands back with MUIM_Application_ReturnID, so main.c's loop gets it back from
   MUIM_Application_NewInput and dispatches on it. They start at 1: 0 means nothing
   happened. Ranges are tested with < and > in places (ID_TABS..ID_TABPICK, ID_SPCHANGE..ID_SPADD,
   ID_MSHAPE..ID_MSHAPE_END), so new ids go at the end of a group, not in the middle */
#ifndef QUILL_IDS_H
#define QUILL_IDS_H
enum {
  ID_NEW = 1, ID_OPEN, ID_SAVE, ID_SAVEAS, ID_SAVETXT, ID_ABOUT, ID_QUIT,
  ID_UNDO, ID_REDO, ID_CUT, ID_COPY, ID_PASTE, ID_SELALL, ID_FIND, ID_FINDNEXT,
  ID_BOLD, ID_ITALIC, ID_UNDERLINE, ID_STRIKE, ID_SUPER, ID_SUB, ID_CAPS,
  ID_LEFT, ID_CENTER, ID_RIGHT, ID_JUSTIFY, ID_SINGLE, ID_ONEHALF, ID_DOUBLE,
  ID_BULLETS, ID_NUMBERS, ID_INDENT, ID_OUTDENT, ID_PAGEBREAK,
  ID_ZOOMFIT, ID_ZOOM75, ID_ZOOM100, ID_ZOOM125, ID_ZOOM150, ID_ZOOM200,
  ID_PAPER0, ID_PAPER1, ID_PAPER2, ID_PAPER3, ID_PORTRAIT, ID_LANDSCAPE, ID_MARGIN0, ID_MARGIN1, ID_MARGIN2, ID_MARGIN3, ID_FINDGO, ID_FINDPREV, ID_REPLACE1, ID_REPLACEALL,
  ID_PDF, ID_PRINT, ID_TABLE, ID_TABLEGO, ID_LINEBREAK, ID_DATE,
  ID_ROW_ABOVE, ID_ROW_BELOW, ID_COL_LEFT, ID_COL_RIGHT, ID_DEL_ROW, ID_DEL_COL, ID_DEL_TABLE, ID_HEADERROW, ID_HEADFOOT, ID_HEADFOOTGO, ID_PICTURE, ID_PICSIZE, ID_PICSIZEGO, ID_NOTE, ID_NOTEGO,
  ID_OUTLINE, ID_RULER, ID_NAVJUMP,
  /* the ribbon, the File page, formatting, references and review */
  ID_GROW, ID_SHRINK, ID_CASE, ID_CLEARFMT, ID_HIGHLIGHT, ID_FONTCOLOR, ID_MARKS, ID_SPACINGDD, ID_STYLEPICK, ID_FONTDD, ID_SIZEDD,
  ID_SELECTDD, ID_REPLACE, ID_MARGINSDD, ID_ORIENTDD, ID_PAPERDD, ID_BREAKSDD, ID_INDENTL, ID_INDENTR, ID_SPACEB, ID_SPACEA,
  ID_TOC, ID_TOCUPDATE, ID_ENDNOTE, ID_NEXTNOTE, ID_SPELL, ID_WORDCOUNT, ID_COMMENT, ID_DELCOMMENT, ID_PREVCOMMENT, ID_NEXTCOMMENT,
  ID_SHOWCOMMENTS, ID_TRACK, ID_ACCEPTDD, ID_REJECTDD, ID_PREVCHANGE, ID_NEXTCHANGE, ID_ZOOMDD, ID_ONEPAGE, ID_MERGE, ID_SPLIT,
  ID_VALIGN0, ID_VALIGN1, ID_VALIGN2, ID_PAGENUM, ID_SYMBOLDD, ID_FILETAB, ID_BACKSTAGE, ID_TEMPLATE, ID_RECENT, ID_STYLEMORE,
  ID_DELTABLEDD, ID_SHADINGDD, ID_NAVSEARCH, ID_NAVPICK, ID_ZOOMSET, ID_FOCUS, ID_OPTIONS, ID_INFO, ID_SORT, ID_BRUSH, ID_PAGEWIDTH, ID_ZOOM100B,
  ID_TABS, ID_TABSET, ID_TABCLEAR, ID_TABCLEARALL, ID_TABPICK, ID_COMMENTGO,
  ID_SPCHANGE, ID_SPCHANGEALL, ID_SPIGNORE, ID_SPIGNOREALL, ID_SPADD,
  ID_VREAD, ID_VPRINT, ID_VWEB, ID_VDRAFT, ID_GRID, ID_DARKPAGE, ID_MULTIPAGE, ID_THEME, ID_LANGUAGE,
  ID_COLUMNSDD, ID_LINENUMDD, ID_HYPHENDD, ID_PAGEALIGNDD, ID_DROPCAPDD, ID_WATERMARKDD, ID_BORDERDD, ID_WMGO, ID_COLBREAK, ID_SECTBREAK,
  /* tables (0.4) */
  ID_TSTYLEDD, ID_LK_FIRSTROW, ID_LK_LASTROW, ID_LK_BANDROWS, ID_LK_FIRSTCOL, ID_LK_LASTCOL, ID_LK_BANDCOLS,
  ID_PENSTYLE, ID_PENWIDTH, ID_PENCOLOR, ID_TBORDERSDD, ID_TSELECTDD, ID_TGRID, ID_TPROPS, ID_TPROPSGO, ID_SPLITTABLE, ID_SPLITGO,
  ID_AUTOFITDD, ID_ROWH, ID_COLW, ID_DISTROWS, ID_DISTCOLS, ID_TSORT, ID_TSORTGO, ID_TOTEXT, ID_TOTEXTGO, ID_FORMULA, ID_FORMULAGO,
  ID_TOTABLE, ID_TOTABLEGO,
  /* pictures and objects (0.5) */
  ID_SHAPESDD, ID_TEXTBOX, ID_OBJFILLDD, ID_OBJLINEDD, ID_OBJWEIGHTDD, ID_OBJDASHDD, ID_OBJPOSDD, ID_OBJWRAPDD, ID_OBJROTDD,
  ID_OBJLAYOUT, ID_OBJCROP, ID_OBJSIZE, ID_ALTTEXT, ID_EDITTEXT, ID_OBJGO, ID_OBJEDIT,
  ID_MSHAPE, ID_MSHAPE_END = ID_MSHAPE + 8,     /* the Insert menu's shapes, in SH_ order */
  /* styles and formatting (0.6) */
  ID_PASTESPECDD, ID_MULTILISTDD, ID_PBORDERDD, ID_PSHADEDD, ID_STYLESETDD, ID_THEMEFONTDD, ID_PSPACINGDD,
  ID_FONTDLG, ID_PARADLG, ID_STYLESPANE, ID_NEWSTYLE, ID_MODSTYLE, ID_FONTGO, ID_PARAGO, ID_BORDERSGO, ID_STYLEGO,
  ID_STYLEAPPLY, ID_STYLEDELETE, ID_STYLEUPDATE, ID_STYLELIST, ID_LISTDEFGO, ID_LISTDEFLVL, ID_NUMVALGO, ID_PASTEKEEP, ID_PASTEMERGE, ID_PASTETEXT,
  ID_BORDERSDLG, ID_LISTDEF, ID_NUMVAL,
  /* window and screen (0.6.1) */
  ID_WINPREFS, ID_WINPREFSGO, ID_WINCUR, ID_WINSCRPICK,
  /* fonts (0.7) */
  ID_FAMPICK,
  /* page furniture (0.10) */
  ID_HFHEADER, ID_HFFOOTER, ID_HFPAGE, ID_HFPAGES, ID_HFPREV, ID_HFNEXT, ID_HFFIRST, ID_HFEVEN, ID_HFOWN,
  ID_STORYCLOSE, ID_STORYDISCARD, ID_STORYREQ, ID_PAGESETUPDLG, ID_PAGESETUPGO, ID_EDITNOTE, ID_EDITCOMMENT,
  /* pictures and angles (0.11) */
  ID_GROUP, ID_UNGROUP,
  /* several documents (0.12) */
  ID_CLOSE, ID_NEXTWIN, ID_SPPICK, ID_CTXPICK,
  /* hyperlinks and the WordCraft comparison's quick items (0.13) */
  ID_LINK, ID_LINKGO, ID_UNLINK, ID_LINKOPEN, ID_LINKCOPY,
  ID_PASTEDD, ID_HEADERDD, ID_FOOTERDD, ID_TOCDD, ID_ADDTEXTDD, ID_FINDDD,      /* (ribbon lists: main.c's ribbon_choice) */
  ID_FORWARDDD, ID_BACKWARDDD, ID_ALIGNOBJDD, ID_GROUPDD, ID_PAGENUMDD, ID_COVERDD,       /* (0.14: ribbon lists) */
  ID_PGFMTGO, ID_THEMEDD, ID_COLORSETDD, ID_PAGECOLORDD, ID_SETDEFAULT, ID_UNDERLINEDD, ID_CBORDER,
  ID_FORWARD, ID_BACKWARD, ID_SELPANE, ID_SELPANEPICK, ID_SELPANEREF,
  ID_BOOKMARK, ID_BMADD, ID_BMDEL, ID_BMGO, ID_BMPICK, ID_XREF, ID_XRINS, ID_XRTYPE,
  ID_BLANKPAGE, ID_GOTO, ID_GOTOGO, ID_DROPCAPDLG, ID_DROPCAPGO, ID_LINENUMDLG, ID_LINENUMGO, ID_MORECOLGO, ID_REMOVETOC,
  /* from 1000: mostly notifications rather than commands (the view's title, outline, table or view
     mode changed), set up with MUIM_Notify in main.c; nothing sends the _CYC ones at present */
  ID_STYLECYC = 1000, ID_SIZECYC, ID_FONTCYC, ID_TITLE, ID_COLORCYC, ID_SPACINGCYC, ID_OUTLINECHG, ID_INTABLE, ID_PAPERCYC, ID_ORIENTCYC, ID_MARGINCYC, ID_ZOOMCYC, ID_VMODECHG,
  /* a document window activated or closed: these plus its slot (main.c's wins[]) */
  ID_WINACT = 3000, ID_WINCLOSE = 3100
};

/* the work packages' blocks (docs/wordcraft-gap.md, "WP0 conventions"): 64 ids each from 4000, in the
   order of WPI_ below, clear of the notifications at 1000 and the window ids at 3000 and 3100. A
   package numbers its own commands inside its block (ID_WP1_FIRST + 0, + 1 ...: name them in its own
   amiga/wp_*.c or a header of its own) and never uses another's; main.c's command() and
   ribbon_choice() send the whole block to the package's wpN_command() (see wphooks.h) */
#define ID_WP_BASE 4000
#define ID_WP_BLOCK 64
#define ID_WP_FIRST_OF(i) (ID_WP_BASE + ID_WP_BLOCK * (i))
#define ID_WP_LAST_OF(i) (ID_WP_BASE + ID_WP_BLOCK * (i) + ID_WP_BLOCK - 1)
/* each package's index (its id block, its ribbon state word, its place in wphooks.c's table) */
enum {
  WPI_1 = 0, WPI_2, WPI_3, WPI_4, WPI_5, WPI_6, WPI_7, WPI_8, WPI_9, WPI_10, WPI_11, WPI_12, WPI_13, WPI_14A, WPI_14B,
  WPI_15A, WPI_15B, WPI_15C, WPI_16, WPI_17, WPI_18, WPI_19, WPI_20, WPI_21A, WPI_21B, WPI_22A, WPI_22B, WPI_23,
  WPI_24, WPI_25, WPI_26, WPI_27, WPI_28A, WPI_28B, WPI_28C, WPI_28D, WPI_29, WPI_30, WPI_31, WP_NPKG
};
enum {
  ID_WP1_FIRST = ID_WP_FIRST_OF(WPI_1), ID_WP1_LAST = ID_WP_LAST_OF(WPI_1),   /* WP1: Fields (wp_fields.c) */
  ID_WP2_FIRST = ID_WP_FIRST_OF(WPI_2), ID_WP2_LAST = ID_WP_LAST_OF(WPI_2),   /* WP2: Captions, tables of figures, index, authorities, custom TOC (wp_refs.c) */
  ID_WP3_FIRST = ID_WP_FIRST_OF(WPI_3), ID_WP3_LAST = ID_WP_LAST_OF(WPI_3),   /* WP3: Citations and bibliography (wp_cite.c) */
  ID_WP4_FIRST = ID_WP_FIRST_OF(WPI_4), ID_WP4_LAST = ID_WP_LAST_OF(WPI_4),   /* WP4: Mail merge, envelopes, labels (wp_mailings.c) */
  ID_WP5_FIRST = ID_WP_FIRST_OF(WPI_5), ID_WP5_LAST = ID_WP_LAST_OF(WPI_5),   /* WP5: Review: comments and markup (wp_review.c) */
  ID_WP6_FIRST = ID_WP_FIRST_OF(WPI_6), ID_WP6_LAST = ID_WP_LAST_OF(WPI_6),   /* WP6: Compare and Combine (wp_compare.c) */
  ID_WP7_FIRST = ID_WP_FIRST_OF(WPI_7), ID_WP7_LAST = ID_WP_LAST_OF(WPI_7),   /* WP7: Protection and encryption (wp_protect.c) */
  ID_WP8_FIRST = ID_WP_FIRST_OF(WPI_8), ID_WP8_LAST = ID_WP_LAST_OF(WPI_8),   /* WP8: Proofing: grammar, thesaurus, AutoCorrect, AutoFormat, Read Aloud (wp_proof.c) */
  ID_WP9_FIRST = ID_WP_FIRST_OF(WPI_9), ID_WP9_LAST = ID_WP_LAST_OF(WPI_9),   /* WP9: Character formatting (wp_charfmt.c) */
  ID_WP10_FIRST = ID_WP_FIRST_OF(WPI_10), ID_WP10_LAST = ID_WP_LAST_OF(WPI_10),  /* WP10: Paragraph and Home quick commands (wp_para.c) */
  ID_WP11_FIRST = ID_WP_FIRST_OF(WPI_11), ID_WP11_LAST = ID_WP_LAST_OF(WPI_11),  /* WP11: Outline view and outline levels (wp_outline.c) */
  ID_WP12_FIRST = ID_WP_FIRST_OF(WPI_12), ID_WP12_LAST = ID_WP_LAST_OF(WPI_12),  /* WP12: Find, Go To, selection and editing keys (wp_find.c) */
  ID_WP13_FIRST = ID_WP_FIRST_OF(WPI_13), ID_WP13_LAST = ID_WP_LAST_OF(WPI_13),  /* WP13: Styles and themes (wp_styles.c) */
  ID_WP14A_FIRST = ID_WP_FIRST_OF(WPI_14A), ID_WP14A_LAST = ID_WP_LAST_OF(WPI_14A), /* WP14a: Equations: model, layout, .docx (wp_math.c) */
  ID_WP14B_FIRST = ID_WP_FIRST_OF(WPI_14B), ID_WP14B_LAST = ID_WP_LAST_OF(WPI_14B), /* WP14b: Equation editor (wp_equation.c) */
  ID_WP15A_FIRST = ID_WP_FIRST_OF(WPI_15A), ID_WP15A_LAST = ID_WP_LAST_OF(WPI_15A), /* WP15a: Charts and SmartArt: read, draw, keep (wp_chartkeep.c) */
  ID_WP15B_FIRST = ID_WP_FIRST_OF(WPI_15B), ID_WP15B_LAST = ID_WP_LAST_OF(WPI_15B), /* WP15b: Insert Chart and Chart Design (wp_chart.c) */
  ID_WP15C_FIRST = ID_WP_FIRST_OF(WPI_15C), ID_WP15C_LAST = ID_WP_LAST_OF(WPI_15C), /* WP15c: Insert SmartArt and SmartArt Design (wp_smartart.c) */
  ID_WP16_FIRST = ID_WP_FIRST_OF(WPI_16), ID_WP16_LAST = ID_WP_LAST_OF(WPI_16),  /* WP16: Content controls (wp_controls.c) */
  ID_WP17_FIRST = ID_WP_FIRST_OF(WPI_17), ID_WP17_LAST = ID_WP_LAST_OF(WPI_17),  /* WP17: Pictures (wp_picture.c) */
  ID_WP18_FIRST = ID_WP_FIRST_OF(WPI_18), ID_WP18_LAST = ID_WP_LAST_OF(WPI_18),  /* WP18: Shapes and text boxes (wp_shapes.c) */
  ID_WP19_FIRST = ID_WP_FIRST_OF(WPI_19), ID_WP19_LAST = ID_WP_LAST_OF(WPI_19),  /* WP19: WordArt and signature line (wp_wordart.c) */
  ID_WP20_FIRST = ID_WP_FIRST_OF(WPI_20), ID_WP20_LAST = ID_WP_LAST_OF(WPI_20),  /* WP20: Draw tab and drawing canvas (wp_draw.c) */
  ID_WP21A_FIRST = ID_WP_FIRST_OF(WPI_21A), ID_WP21A_LAST = ID_WP_LAST_OF(WPI_21A), /* WP21a: Table styles and quick tables (wp_tables.c) */
  ID_WP21B_FIRST = ID_WP_FIRST_OF(WPI_21B), ID_WP21B_LAST = ID_WP_LAST_OF(WPI_21B), /* WP21b: Table drawing and cell options (wp_tables2.c) */
  ID_WP22A_FIRST = ID_WP_FIRST_OF(WPI_22A), ID_WP22A_LAST = ID_WP_LAST_OF(WPI_22A), /* WP22a: Pages, sections and columns (wp_pages.c) */
  ID_WP22B_FIRST = ID_WP_FIRST_OF(WPI_22B), ID_WP22B_LAST = ID_WP_LAST_OF(WPI_22B), /* WP22b: Footnote and endnote options (wp_notes.c) */
  ID_WP23_FIRST = ID_WP_FIRST_OF(WPI_23), ID_WP23_LAST = ID_WP_LAST_OF(WPI_23),  /* WP23: Templates, building blocks, inserting files (wp_templates.c) */
  ID_WP24_FIRST = ID_WP_FIRST_OF(WPI_24), ID_WP24_LAST = ID_WP_LAST_OF(WPI_24),  /* WP24: Document safety and information (wp_info.c) */
  ID_WP25_FIRST = ID_WP_FIRST_OF(WPI_25), ID_WP25_LAST = ID_WP_LAST_OF(WPI_25),  /* WP25: Views and windows (wp_windows.c) */
  ID_WP26_FIRST = ID_WP_FIRST_OF(WPI_26), ID_WP26_LAST = ID_WP_LAST_OF(WPI_26),  /* WP26: Customization and discoverability (wp_custom.c) */
  ID_WP27_FIRST = ID_WP_FIRST_OF(WPI_27), ID_WP27_LAST = ID_WP_LAST_OF(WPI_27),  /* WP27: Right-to-left and East Asian (wp_intl.c) */
  ID_WP28A_FIRST = ID_WP_FIRST_OF(WPI_28A), ID_WP28A_LAST = ID_WP_LAST_OF(WPI_28A), /* WP28a: RTF (wp_rtf.c) */
  ID_WP28B_FIRST = ID_WP_FIRST_OF(WPI_28B), ID_WP28B_LAST = ID_WP_LAST_OF(WPI_28B), /* WP28b: ODT (wp_odt.c) */
  ID_WP28C_FIRST = ID_WP_FIRST_OF(WPI_28C), ID_WP28C_LAST = ID_WP_LAST_OF(WPI_28C), /* WP28c: HTML and Markdown (wp_html.c) */
  ID_WP28D_FIRST = ID_WP_FIRST_OF(WPI_28D), ID_WP28D_LAST = ID_WP_LAST_OF(WPI_28D), /* WP28d: Word 97-2003 .doc reader (wp_docbin.c) */
  ID_WP29_FIRST = ID_WP_FIRST_OF(WPI_29), ID_WP29_LAST = ID_WP_LAST_OF(WPI_29),  /* WP29: PDF export depth and page images (wp_pdf.c) */
  ID_WP30_FIRST = ID_WP_FIRST_OF(WPI_30), ID_WP30_LAST = ID_WP_LAST_OF(WPI_30),  /* WP30: More interface languages (wp_uilang.c) */
  ID_WP31_FIRST = ID_WP_FIRST_OF(WPI_31), ID_WP31_LAST = ID_WP_LAST_OF(WPI_31),  /* WP31: Proofing languages (wp_proflang.c) */
  ID_WP_FIRST = ID_WP_BASE, ID_WP_LAST = ID_WP_LAST_OF(WP_NPKG - 1)
};

#endif
