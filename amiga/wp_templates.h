/* wp_templates.h - WP23's command ids and what backstage.c needs of it (the File page's personal
   templates on the New page, Save as Template on the Export page). Belongs to WP23 */
#ifndef QUILL_WP_TEMPLATES_H
#define QUILL_WP_TEMPLATES_H
#include "ids.h"
#define WP23_NPERSONAL 12                /* personal templates the New page lists */
enum { ID_TP_PERSONAL0 = ID_WP23_FIRST, ID_TP_SAVEAS = ID_WP23_FIRST + WP23_NPERSONAL, ID_TP_FIRSTOTHER };
/* the i'th template in the personal templates folder (PROGDIR:Templates): its name (Latin-1, without
   the ending) in name; 0 past the last */
int wp23_personal(int i, char *name, size_t cap);
#endif
