/* wp_info.h - WP24's command ids and what ribbon.c and backstage.c need of it (the quick access
   AutoSave button, the File page's Info buttons and Options). Belongs to WP24 */
#ifndef QUILL_WP_INFO_H
#define QUILL_WP_INFO_H
#include "ids.h"
enum {
  ID_INFO_TICK = ID_WP24_FIRST, ID_INFO_AUTOSAVE, ID_INFO_PROPS, ID_INFO_PROPSOK, ID_INFO_CUSTADD, ID_INFO_CUSTDEL, ID_INFO_CUSTPICK,
  ID_INFO_INSPECT, ID_INFO_INSPGO, ID_INFO_INSPREMOVE,
  ID_INFO_ACC, ID_INFO_ACCGO, ID_INFO_ACCPICK, ID_INFO_ACCALT, ID_INFO_ACCDECOR, ID_INFO_ACCAGAIN,
  ID_INFO_COMPAT, ID_INFO_VERSIONS, ID_INFO_VEROPEN, ID_INFO_VERRESTORE, ID_INFO_VERDEL,
  ID_INFO_RECOVER, ID_INFO_RECOPEN, ID_INFO_RECDEL, ID_INFO_ALTPANE, ID_INFO_ALTSET, ID_INFO_ALTDECOR
};
#define WP24S_AUTOSAVE 1                 /* WP24's ribbon state bit: AutoSave is on */
/* the AutoSave switch's icon (theme.c's icon language): a toggle */
#define WP24_AUTOSAVE_ICON "ci R 1 5 14 11 ca F 8 6 13 10 ci L 3 8 5 8"
int wp24_autosave_on(void);              /* AutoSave on (the File page's Options show it) */
void wp24_autosave_set(int on);          /* turned on or off (and remembered) */
#endif
