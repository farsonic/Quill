# tools/lang/mac.py - the interface words of Quill for the classic Mac OS (mac/*.c) that the Amiga's
# don't have, in the same form as tools/mklang.py's W (English, German, French, Spanish, Italian),
# Latin-1 only (mac/qm_lang.c turns the table into Mac Roman). The Mac's menus otherwise reuse the
# Amiga's words. After changing it run python3 tools/mklang.py.
W = [
("Quill 0.15 for the Mac\\n\\n%s", "Quill 0.15 für den Mac\\n\\n%s", "Quill 0.15 pour le Mac\\n\\n%s", "Quill 0.15 para el Mac\\n\\n%s", "Quill 0.15 per il Mac\\n\\n%s"),
("A word processor for the classic Mac OS.\\nReads and writes Word documents (.docx).\\n\\nText is measured with the Liberation and Carlito/Caladea fonts\\n(SIL Open Font License), so documents break\\ntheir lines as they do in Word.",
 "Eine Textverarbeitung für das klassische Mac OS.\\nLiest und schreibt Word-Dokumente (.docx).\\n\\nText wird mit den Schriften Liberation und Carlito/Caladea\\ngemessen (SIL Open Font License), daher brechen\\nZeilen wie in Word um.",
 "Un traitement de texte pour le Mac OS classique.\\nLit et écrit les documents Word (.docx).\\n\\nLe texte est mesuré avec les polices Liberation et Carlito/Caladea\\n(SIL Open Font License) : les lignes se coupent\\ncomme dans Word.",
 "Un procesador de textos para el Mac OS clásico.\\nLee y escribe documentos de Word (.docx).\\n\\nEl texto se mide con las fuentes Liberation y Carlito/Caladea\\n(SIL Open Font License), así las líneas\\nse cortan como en Word.",
 "Un elaboratore di testi per il Mac OS classico.\\nLegge e scrive documenti Word (.docx).\\n\\nIl testo è misurato con i caratteri Liberation e Carlito/Caladea\\n(SIL Open Font License), così le righe\\nvanno a capo come in Word."),
# Format > Paragraph Style (a submenu, and the strip's style pop-up shows the same styles)
("Paragraph Style", "Absatzformat", "Style de paragraphe", "Estilo de párrafo", "Stile di paragrafo"),
# the Style menu of the Guidelines (p. 113): the text's style; the table's "Style" is a paragraph style
# (German Formatvorlage), and the Mac's localised systems call this menu Stil
("menu:Style", "Stil", "Style", "Estilo", "Stile"),
# File > Revert (the Guidelines' File menu) and its question; Cancel is the default (*), as Revert loses changes (p. 206)
("Revert", "Alte Version", "Version précédente", "Recuperar", "Ripristina"),
("Revert|*Cancel", "Alte Version|*Abbrechen", "Version précédente|*Annuler", "Recuperar|*Cancelar", "Ripristina|*Annulla"),
("Revert to the saved version of %s? The changes made since are lost.",
 "Zur gesicherten Version von %s zurückkehren? Die Änderungen seither gehen verloren.",
 "Revenir à la version enregistrée de %s ? Les modifications faites depuis seront perdues.",
 "¿Volver a la versión guardada de %s? Se perderán los cambios hechos desde entonces.",
 "Tornare alla versione salvata di %s? Le modifiche fatte da allora andranno perse."),
# View > Tool Strip: which of the strips (Home, Insert, Layout, References, Review) the window shows
("Tool Strip", "Symbolleiste", "Barre d'outils", "Barra de herramientas", "Barra degli strumenti"),
# Insert > Table..., Footnote..., Endnote... (mac/qm_cmds.c)
("Rows:", "Zeilen:", "Lignes :", "Filas:", "Righe:"),
("Columns are 1 to 64 and rows 1 to 500.", "Spalten: 1 bis 64, Zeilen: 1 bis 500.", "Colonnes : de 1 à 64, lignes : de 1 à 500.", "Columnas: de 1 a 64; filas: de 1 a 500.", "Colonne da 1 a 64, righe da 1 a 500."),
("Footnote text:", "Text der Fußnote:", "Texte de la note de bas de page :", "Texto de la nota al pie:", "Testo della nota a piè di pagina:"),
("Endnote text:", "Text der Endnote:", "Texte de la note de fin :", "Texto de la nota al final:", "Testo della nota di chiusura:"),
# Layout > Page Setup... and Paragraph... (mac/qm_cmds.c)
("Margins (cm):", "Ränder (cm):", "Marges (cm) :", "Márgenes (cm):", "Margini (cm):"),
("Margins (inches):", "Ränder (Zoll):", "Marges (pouces) :", "Márgenes (pulgadas):", "Margini (pollici):"),
("The margins must be numbers, and leave room for text on the page.", "Die Ränder müssen Zahlen sein und Platz für Text auf der Seite lassen.", "Les marges doivent être des nombres et laisser de la place au texte sur la page.", "Los márgenes deben ser números y dejar sitio al texto en la página.", "I margini devono essere numeri e lasciare spazio al testo sulla pagina."),
("Indentation (cm):", "Einzug (cm):", "Retrait (cm) :", "Sangría (cm):", "Rientro (cm):"),
("Indentation (inches):", "Einzug (Zoll):", "Retrait (pouces) :", "Sangría (pulgadas):", "Rientro (pollici):"),
("First line:", "Erste Zeile:", "Première ligne :", "Primera línea:", "Prima riga:"),
("Spacing (points):", "Abstand (Punkt):", "Espacement (points) :", "Espaciado (puntos):", "Spaziatura (punti):"),
("Indents and spacing must be numbers (spacing not below 0).", "Einzüge und Abstände müssen Zahlen sein (Abstände nicht unter 0).", "Les retraits et espacements doivent être des nombres (espacement d'au moins 0).", "Las sangrías y el espaciado deben ser números (espaciado no inferior a 0).", "Rientri e spaziatura devono essere numeri (spaziatura non inferiore a 0)."),
]
