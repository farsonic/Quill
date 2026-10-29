/*
 * wphooks.c - the work packages' table and the calls that hand each package what is its own (see
 * wphooks.h): its commands, ribbon states and fields, menu items, right-button menu items and keys.
 * WP0 made it; the packages don't edit it: each fills in its own wpN_package in amiga/wp_<name>.c.
 */
#include <string.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"

extern const WpPackage wp1_package, wp2_package, wp3_package, wp4_package, wp5_package, wp6_package, wp7_package,
  wp8_package, wp9_package, wp10_package, wp11_package, wp12_package, wp13_package, wp14a_package, wp14b_package,
  wp15a_package, wp15b_package, wp15c_package, wp16_package, wp17_package, wp18_package, wp19_package, wp20_package,
  wp21a_package, wp21b_package, wp22a_package, wp22b_package, wp23_package, wp24_package, wp25_package, wp26_package,
  wp27_package, wp28a_package, wp28b_package, wp28c_package, wp28d_package, wp29_package, wp30_package, wp31_package;
/* in WPI_ order (ids.h) */
static const WpPackage *const packages[WP_NPKG] = {
  &wp1_package, &wp2_package, &wp3_package, &wp4_package, &wp5_package, &wp6_package, &wp7_package, &wp8_package,
  &wp9_package, &wp10_package, &wp11_package, &wp12_package, &wp13_package, &wp14a_package, &wp14b_package,
  &wp15a_package, &wp15b_package, &wp15c_package, &wp16_package, &wp17_package, &wp18_package, &wp19_package,
  &wp20_package, &wp21a_package, &wp21b_package, &wp22a_package, &wp22b_package, &wp23_package, &wp24_package,
  &wp25_package, &wp26_package, &wp27_package, &wp28a_package, &wp28b_package, &wp28c_package, &wp28d_package,
  &wp29_package, &wp30_package, &wp31_package
};

const WpPackage *wp_package(int i) { return i >= 0 && i < WP_NPKG ? packages[i] : NULL; }
/* the package whose block id is in (NULL: none) */
static const WpPackage *owner(ULONG id)
{
  if (id < ID_WP_FIRST || id > ID_WP_LAST) return NULL;
  return packages[(id - ID_WP_FIRST) / ID_WP_BLOCK];
}
/* whether id is a list's (a drop-down or field) in its package's ribbon groups: its choice comes with
   the entry's number, which main.c reads from the ribbon */
int wp_is_list(ULONG id)
{
  const WpPackage *p = owner(id);
  int g, i;
  if (!p) return 0;
  for (g = 0; g < p->ngroups; g++)
    for (i = 0; i < p->groups[g].g.n; i++) {
      const Item *it = &p->groups[g].g.items[i];
      if (it->id == id && (it->kind == K_DROP || it->kind == K_FIELD || it->kind == K_GALLERY)) return 1;
    }
  return 0;
}
/* a command of a package's: to its wpN_command (WPR_DONE if it has none) */
int wp_run(ULONG id, LONG value)
{
  const WpPackage *p = owner(id);
  return p && p->command ? p->command(id, value) : WPR_DONE;
}
/* each package's state word (cleared first: a package without a state hook has none on) */
void wp_state(ULONG *wpon, const WpCtx *c)
{
  int i;
  for (i = 0; i < WP_NPKG; i++) {
    wpon[i] = 0;
    if (packages[i]->state) packages[i]->state(&wpon[i], c);
  }
}
void wp_field_text(ULONG id, const struct MUIP_Ribbon_State *st, char *out, size_t cap)
{
  const WpPackage *p = owner(id);
  if (cap) out[0] = 0;
  if (p && p->field) p->field(id, st, out, cap);
}
void wp_ctxmenu(Object *menu, const WpCtx *c)
{
  int i;
  for (i = 0; i < WP_NPKG; i++) if (packages[i]->ctxmenu) packages[i]->ctxmenu(menu, c);
}
/* the first package that takes the key */
ULONG wp_key(UWORD code, UWORD qual, const WpCtx *c)
{
  int i;
  for (i = 0; i < WP_NPKG; i++)
    if (packages[i]->key) {
      ULONG id = packages[i]->key(code, qual, c);
      if (id) return id;
    }
  return 0;
}
void wp_init(void)
{
  int i;
  for (i = 0; i < WP_NPKG; i++) if (packages[i]->init) packages[i]->init();
}
void wp_cleanup(void)
{
  int i;
  for (i = 0; i < WP_NPKG; i++) if (packages[i]->cleanup) packages[i]->cleanup();
}

/* the menu bar: base's titles and items, each title's followed by the items the packages add to it
   (WpMenu.menu: the title's number), in package order. Made once */
struct NewMenu *wp_menus(struct NewMenu *base)
{
  static struct NewMenu *merged;
  int nbase, nadd = 0, i, k, t, o;
  if (merged) return merged;
  for (nbase = 0; base[nbase].nm_Type != NM_END; nbase++) ;
  for (i = 0; i < WP_NPKG; i++) nadd += packages[i]->nmenus;
  if (!nadd || !(merged = (struct NewMenu *)malloc((size_t)(nbase + nadd + 1) * sizeof *merged))) return base;
  o = 0; t = -1;
  for (k = 0; k <= nbase; k++) {
    if (k == nbase || base[k].nm_Type == NM_TITLE) {
      /* the end of title t's items: the packages' */
      if (t >= 0)
        for (i = 0; i < WP_NPKG; i++) {
          int m;
          for (m = 0; m < packages[i]->nmenus; m++) if (packages[i]->menus[m].menu == t) merged[o++] = packages[i]->menus[m].nm;
        }
      t++;
    }
    merged[o++] = base[k];               /* (the last is base's NM_END) */
  }
  return merged;
}

/* the right-button menu: an item (label NM_BARLABEL: a separator), greyed if not enabled */
void wp_ctx_item(Object *menu, const char *label, ULONG userdata, int enabled)
{
  Object *it = MenuitemObject, MUIA_Menuitem_Title, (ULONG)label, MUIA_UserData, userdata, MUIA_Menuitem_Enabled, enabled, End;
  if (it) DoMethod(menu, MUIM_Family_AddTail, (ULONG)it);
}
/* an item with a submenu of entries (translated), entry i running id with value i */
void wp_ctx_sub(Object *menu, const char *label, ULONG id, const char *const *labels, int n)
{
  Object *it = MenuitemObject, MUIA_Menuitem_Title, (ULONG)label, End;
  int i;
  if (!it) return;
  for (i = 0; i < n && i < 256; i++) {
    Object *s = MenuitemObject, MUIA_Menuitem_Title, (ULONG)tr(labels[i]), MUIA_UserData, WP_CTX_LIST(id, i), End;
    if (s) DoMethod(it, MUIM_Family_AddTail, (ULONG)s);
  }
  DoMethod(menu, MUIM_Family_AddTail, (ULONG)it);
}
