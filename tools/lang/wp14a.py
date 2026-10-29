# tools/lang/wp14a.py - the interface words of WP14a Equations: model, layout, .docx (amiga/wp_math.c and its other
# files), in the same form as tools/mklang.py's W: (English as the C source writes it, German, French,
# Spanish, Italian), Word's own localized terms, Latin-1 only. This file belongs to WP14a. After
# changing it run python3 tools/mklang.py, which writes these rows into WP14a's section of
# amiga/lang_table.h.
W = [
("Equation", "Formel", "Équation", "Ecuación", "Equazione"),
("Equation...", "Formel...", "Équation...", "Ecuación...", "Equazione..."),
("Equation:", "Formel:", "Équation :", "Ecuación:", "Equazione:"),
("Edit Equation...", "Formel bearbeiten...", "Modifier l'équation...", "Editar ecuación...", "Modifica equazione..."),
("Insert an equation, typed in linear format", "Eine Formel im linearen Format eingeben und einfügen", "Insérer une équation tapée au format linéaire",
 "Insertar una ecuación escrita en formato lineal", "Inserisci un'equazione digitata in formato lineare"),
("Type the equation in linear format, for example:", "Geben Sie die Formel im linearen Format ein, zum Beispiel:",
 "Tapez l'équation au format linéaire, par exemple :", "Escriba la ecuación en formato lineal, por ejemplo:",
 "Digitare l'equazione in formato lineare, ad esempio:"),
("x^2+(a+b)/c   \\\\sqrt(x)   \\\\sum_(i=1)^n i   \\\\matrix(a&b@c&d)", None, None, None, None),
]
