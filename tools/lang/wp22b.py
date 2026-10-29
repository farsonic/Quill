# tools/lang/wp22b.py - the interface words of WP22b Footnote and endnote options (amiga/wp_notes.c and its other
# files), in the same form as tools/mklang.py's W: (English as the C source writes it, German, French,
# Spanish, Italian), Word's own localized terms, Latin-1 only. This file belongs to WP22b. After
# changing it run python3 tools/mklang.py, which writes these rows into WP22b's section of
# amiga/lang_table.h.
W = [
("The document has no footnotes or endnotes.", "Das Dokument hat keine Fuß- oder Endnoten.", "Le document n'a pas de notes de bas de page ni de notes de fin.", "El documento no tiene notas al pie ni notas al final.", "Il documento non ha note a piè di pagina né note di chiusura."),
("_Footnotes|_Endnotes|_Cancel", "_Fußnoten|_Endnoten|_Abbrechen", "Notes de _bas de page|Notes de _fin|_Annuler", "Notas al _pie|Notas al _final|_Cancelar", "Note a _piè di pagina|Note di _chiusura|_Annulla"),
("The document has footnotes and endnotes.\nWhich do you want to see?", "Das Dokument enthält Fuß- und Endnoten.\nWelche möchten Sie anzeigen?", "Le document contient des notes de bas de page et des notes de fin.\nLesquelles voulez-vous afficher ?", "El documento contiene notas al pie y notas al final.\n¿Cuáles desea ver?", "Il documento contiene note a piè di pagina e note di chiusura.\nQuali visualizzare?"),
("Show Notes", "Notizen anzeigen", "Afficher les notes", "Mostrar notas", "Mostra note"),
("Show Notes: to the footnotes or endnotes, and back to the text", "Notizen anzeigen: zu den Fuß- oder Endnoten und zurück zum Text", "Afficher les notes : aller aux notes de bas de page ou de fin, et revenir au texte", "Mostrar notas: ir a las notas al pie o al final, y volver al texto", "Mostra note: andare alle note a piè di pagina o di chiusura e tornare al testo"),
("Footnote Options", "Fußnotenoptionen", "Options des notes", "Opciones de notas al pie", "Opzioni note"),
("Footnote and Endnote: where notes go, how they are numbered", "Fuß- und Endnote: wohin Notizen kommen, wie sie nummeriert werden", "Note de bas de page et de fin : où vont les notes, comment elles sont numérotées", "Notas al pie y al final: dónde van las notas y cómo se numeran", "Note a piè di pagina e di chiusura: dove vanno le note e come sono numerate"),
("Notes", "Notizen", "Notes", "Notas", "Note"),
("Footnote and Endnote...", "Fuß- und Endnote...", "Note de bas de page et de fin...", "Notas al pie y al final...", "Note a piè di pagina e di chiusura..."),
("Footnote and Endnote", "Fuß- und Endnote", "Note de bas de page et de fin", "Notas al pie y al final", "Note a piè di pagina e di chiusura"),
("Bottom of page", "Seitenende", "Bas de page", "Final de página", "Fine pagina"),
("Below text", "Unterhalb des Texts", "Sous le texte", "Debajo del texto", "Sotto il testo"),
("End of document", "Dokumentende", "Fin du document", "Final del documento", "Fine documento"),
("End of section", "Abschnittsende", "Fin de la section", "Final de la sección", "Fine sezione"),
("Endnotes", "Endnoten", "Notes de fin", "Notas al final", "Note di chiusura"),
("Symbols (*, ...)", "Symbole (*, ...)", "Symboles (*, ...)", "Símbolos (*, ...)", "Simboli (*, ...)"),
("Restart each section", "Jeden Abschnitt neu beginnen", "Recommencer à chaque section", "Reiniciar cada sección", "Ricomincia a ogni sezione"),
("Restart each page", "Jede Seite neu beginnen", "Recommencer à chaque page", "Reiniciar cada página", "Ricomincia a ogni pagina"),
("Change", "Ändern", "Modifier", "Cambiar", "Modifica"),
("Apply changes", "Änderungen übernehmen", "Appliquer les modifications", "Aplicar cambios", "Applica modifiche"),
("Apply changes to", "Änderungen übernehmen für", "Appliquer les modifications à", "Aplicar cambios a", "Applica modifiche a"),
("_Insert", "_Einfügen", "_Insérer", "_Insertar", "_Inserisci"),
]
