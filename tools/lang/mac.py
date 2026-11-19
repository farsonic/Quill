# tools/lang/mac.py - the interface words of Octavo, Quill for the classic Mac OS (mac/*.c), that the Amiga's
# don't have, in the same form as tools/mklang.py's W (English, German, French, Spanish, Italian),
# Latin-1 only (mac/qm_lang.c turns the table into Mac Roman). The Mac's menus otherwise reuse the
# Amiga's words. After changing it run python3 tools/mklang.py.
W = [
("Octavo 0.15\\n\\n%s", "Octavo 0.15\\n\\n%s", "Octavo 0.15\\n\\n%s", "Octavo 0.15\\n\\n%s", "Octavo 0.15\\n\\n%s"),
("Octavo is Quill for the classic Mac OS: a word processor that reads and writes Word documents (.docx).\\n\\nText is measured with the Liberation and Carlito/Caladea fonts (SIL Open Font License), so documents break their lines as they do in Word.",
 "Octavo ist Quill für das klassische Mac OS: eine Textverarbeitung, die Word-Dokumente (.docx) liest und schreibt.\\n\\nText wird mit den Schriften Liberation und Carlito/Caladea gemessen (SIL Open Font License), daher brechen Zeilen wie in Word um.",
 "Octavo est Quill pour le Mac OS classique : un traitement de texte qui lit et écrit les documents Word (.docx).\\n\\nLe texte est mesuré avec les polices Liberation et Carlito/Caladea (SIL Open Font License) : les lignes se coupent comme dans Word.",
 "Octavo es Quill para el Mac OS clásico: un procesador de textos que lee y escribe documentos de Word (.docx).\\n\\nEl texto se mide con las fuentes Liberation y Carlito/Caladea (SIL Open Font License), así las líneas se cortan como en Word.",
 "Octavo è Quill per il Mac OS classico: un elaboratore di testi che legge e scrive documenti Word (.docx).\\n\\nIl testo è misurato con i caratteri Liberation e Carlito/Caladea (SIL Open Font License), così le righe vanno a capo come in Word."),
("%s\\nis not a PNG or JPEG picture Octavo can read.", "%s\\nist kein PNG- oder JPEG-Bild, das Octavo lesen kann.", "%s\\nn'est pas une image PNG ou JPEG lisible par Octavo.", "%s\\nno es una imagen PNG o JPEG que Octavo pueda leer.", "%s\\nnon è un'immagine PNG o JPEG leggibile da Octavo."),
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
# Edit > Find..., Find Again, Replace... and Format > Font... (mac/qm_cmds.c, mac/qm_main.c)
("Find Again", "Weitersuchen", "Rechercher le suivant", "Buscar siguiente", "Trova successivo"),
("Whole words only", "Nur ganze Wörter", "Mots entiers seulement", "Sólo palabras completas", "Solo parole intere"),
("\"%s\" was not found.", "\"%s\" wurde nicht gefunden.", "\"%s\" est introuvable.", "No se encontró \"%s\".", "\"%s\" non è stato trovato."),
("Replacements made: %s.", "Ersetzungen: %s.", "Remplacements effectués : %s.", "Reemplazos hechos: %s.", "Sostituzioni eseguite: %s."),
("The size must be a number of points from 1 to 1638 (halves allowed).", "Die Größe muss eine Punktzahl von 1 bis 1638 sein (halbe Punkte erlaubt).", "La taille doit être un nombre de points de 1 à 1638 (demi-points permis).", "El tamaño debe ser un número de puntos de 1 a 1638 (se admiten medios).", "La dimensione deve essere un numero di punti da 1 a 1638 (mezzi punti ammessi)."),
# References > Caption..., Bookmark..., Cross-reference... (mac/qm_cmds.c)
("Text:", "Text:", "Texte :", "Texto:", "Testo:"),
("Bookmark name:", "Name der Textmarke:", "Nom du signet :", "Nombre del marcador:", "Nome del segnalibro:"),
("Add", "Hinzufügen", "Ajouter", "Agregar", "Aggiungi"),
("Bookmarks:", "Textmarken:", "Signets :", "Marcadores:", "Segnalibri:"),
("This document has no bookmarks.", "Dieses Dokument hat keine Textmarken.", "Ce document n'a pas de signets.", "Este documento no tiene marcadores.", "Questo documento non ha segnalibri."),
("A bookmark's name is one word (letters, digits and _), on a word of the text.", "Der Name einer Textmarke ist ein Wort (Buchstaben, Ziffern und _) und steht auf einem Wort des Textes.", "Le nom d'un signet est un mot (lettres, chiffres et _), placé sur un mot du texte.", "El nombre de un marcador es una palabra (letras, cifras y _), sobre una palabra del texto.", "Il nome di un segnalibro è una parola (lettere, cifre e _), su una parola del testo."),
# Review (mac/qm_cmds.c)
("Octavo's word list (british-english, in a folder named dict beside Octavo) couldn't be read.", "Octavos Wortliste (british-english, in einem Ordner namens dict neben Octavo) konnte nicht gelesen werden.", "La liste de mots d'Octavo (british-english, dans un dossier nommé dict à côté d'Octavo) n'a pas pu être lue.", "No se pudo leer la lista de palabras de Octavo (british-english, en una carpeta llamada dict junto a Octavo).", "Non è stato possibile leggere l'elenco di parole di Octavo (british-english, in una cartella chiamata dict accanto a Octavo)."),
("The spelling check is complete.", "Die Rechtschreibprüfung ist abgeschlossen.", "La vérification orthographique est terminée.", "La revisión ortográfica ha terminado.", "Il controllo ortografico è terminato."),
("Not in the dictionary:", "Nicht im Wörterbuch:", "Absent du dictionnaire :", "No está en el diccionario:", "Non nel dizionario:"),
("No suggestions.", "Keine Vorschläge.", "Aucune suggestion.", "Sin sugerencias.", "Nessun suggerimento."),
("Words: %lu\\nParagraphs: %lu\\nPages: %lu", "Wörter: %lu\\nAbsätze: %lu\\nSeiten: %lu", "Mots : %lu\\nParagraphes : %lu\\nPages : %lu", "Palabras: %lu\\nPárrafos: %lu\\nPáginas: %lu", "Parole: %lu\\nParagrafi: %lu\\nPagine: %lu"),
("Comment by %s:\\n%s", "Kommentar von %s:\\n%s", "Commentaire de %s :\\n%s", "Comentario de %s:\\n%s", "Commento di %s:\\n%s"),
("Previous Comment", "Vorheriger Kommentar", "Commentaire précédent", "Comentario anterior", "Commento precedente"),
("Next Comment", "Nächster Kommentar", "Commentaire suivant", "Comentario siguiente", "Commento successivo"),
("Previous Change", "Vorherige Änderung", "Modification précédente", "Cambio anterior", "Modifica precedente"),
]
