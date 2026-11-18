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
# ---- WP28e (2026-11): plain text's encoding on Save As Text (amiga/main.c), Markdown Source (amiga/wp_html.c)
("UTF-8|Latin-1|Cancel", "UTF-8|Latin-1|Abbrechen", "UTF-8|Latin-1|Annuler", "UTF-8|Latin-1|Cancelar", "UTF-8|Latin-1|Annulla"),
("UTF-8|Latin-1|UTF-16|Cancel", "UTF-8|Latin-1|UTF-16|Abbrechen", "UTF-8|Latin-1|UTF-16|Annuler", "UTF-8|Latin-1|UTF-16|Cancelar", "UTF-8|Latin-1|UTF-16|Annulla"),
("Save the text in which encoding?\\nUTF-8 keeps every character; Latin-1 is\\nwhat most Amiga editors read.%s", "In welcher Codierung speichern?\\nUTF-8 behält jedes Zeichen; Latin-1 lesen\\ndie meisten Amiga-Editoren.%s", "Enregistrer le texte dans quel codage ?\\nUTF-8 garde tous les caractères ; Latin-1 est\\nce que lisent la plupart des éditeurs Amiga.%s", "¿En qué codificación guardar el texto?\\nUTF-8 conserva todos los caracteres; Latin-1 es\\nlo que leen casi todos los editores de Amiga.%s", "In quale codifica salvare il testo?\\nUTF-8 conserva ogni carattere; Latin-1 è\\nciò che legge la maggior parte degli editor Amiga.%s"),
]
