# tools/lang/wp28c.py - the interface words of WP28c HTML and Markdown (amiga/wp_html.c and its other
# files), in the same form as tools/mklang.py's W: (English as the C source writes it, German, French,
# Spanish, Italian), Word's own localized terms, Latin-1 only. This file belongs to WP28c. After
# changing it run python3 tools/mklang.py, which writes these rows into WP28c's section of
# amiga/lang_table.h.
W = [
("Save As Web Page...", "Als Webseite speichern...", "Enregistrer en page Web...", "Guardar como página web...", "Salva come pagina Web..."),
("Save As Markdown...", "Als Markdown speichern...", "Enregistrer en Markdown...", "Guardar como Markdown...", "Salva come Markdown..."),
("Save as a web page", "Als Webseite speichern", "Enregistrer comme page Web", "Guardar como página web", "Salva come pagina Web"),
("Save as Markdown", "Als Markdown speichern", "Enregistrer en Markdown", "Guardar como Markdown", "Salva come Markdown"),
]
