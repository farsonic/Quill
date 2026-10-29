/*
 * wp_protect.h - WP7 Protection and encryption (docs/wordcraft-gap.md): its command ids (ids.h's block
 * ID_WP7_FIRST..) and what view.c and backstage.c ask of it. This file belongs to WP7.
 */
#ifndef QUILL_WP_PROTECT_H
#define QUILL_WP_PROTECT_H

#include "ids.h"
#include "../core/protect.h"

enum {
  ID_PROT_PANE = ID_WP7_FIRST,     /* Review > Restrict Editing: the pane opened or closed */
  ID_PROT_DOC,                     /* File > Info > Protect Document: its window */
  ID_PROT_ENCRYPT,                 /* Encrypt with Password */
  ID_PROT_ENCRYPTGO,
  ID_PROT_KIND, ID_PROT_ONLY, ID_PROT_FORMAT,   /* the pane's choices */
  ID_PROT_EVERYONE, ID_PROT_USERADD, ID_PROT_USERAPPLY, ID_PROT_USERDEL, ID_PROT_CLEAR,
  ID_PROT_START, ID_PROT_STOP,
  ID_PROT_NEXT, ID_PROT_ALL, ID_PROT_SHADE,
  ID_PROT_PWOK, ID_PROT_PWCANCEL,  /* the password requesters */
  ID_PROT_SHOWPANE,                /* (a protected document came up: the pane shown) */
  ID_PROT_DOCPANE                  /* (the Protect Document window's Restrict Editing button) */
};

/* (view.c) the regions the user may edit are shaded: protection enforced, Highlight on */
int wp7_shade(Doc *d);

#endif
