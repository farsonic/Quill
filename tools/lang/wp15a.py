# tools/lang/wp15a.py - the interface words of WP15a Charts and SmartArt: read, draw, keep (amiga/wp_chartkeep.c and its other
# files), in the same form as tools/mklang.py's W: (English as the C source writes it, German, French,
# Spanish, Italian), Word's own localized terms, Latin-1 only. This file belongs to WP15a. After
# changing it run python3 tools/mklang.py, which writes these rows into WP15a's section of
# amiga/lang_table.h.
W = [
# the right-button menu's title for a chart or SmartArt (amiga/view.c ctx_object)
("Chart", "Diagramm", "Graphique", "Gráfico", "Grafico"),
("SmartArt", "SmartArt", "SmartArt", "SmartArt", "SmartArt"),
]
