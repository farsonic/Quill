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
]
