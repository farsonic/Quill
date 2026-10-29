/* inspect.c - WP24, see inspect.h. The accessibility rules follow Word's checker and WordCraft's
   (crates/engine/src/cmd/accessibility.rs, whose wording is its own); written afresh here in C */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "inspect.h"
#include "info.h"
#include "keep.h"
#include "notes.h"

/* ---- Inspect Document */
static u32 props_count(const Doc *d)
{
  u32 n = 0;
  int k;
  static const int which[] = { IP_SUBJECT, IP_KEYWORDS, IP_DESCRIPTION, IP_CATEGORY, IP_STATUS, IP_LASTBY, IP_COMPANY, IP_MANAGER };
  if (d->author[0]) n++;
  for (k = 0; k < (int)(sizeof which / sizeof which[0]); k++) if (info_str(d, which[k])[0]) n++;
  if (d->info) n += (u32)d->info->ncust;
  for (k = 0; k < d->ncomments; k++) if (d->comments[k].author[0] && strcmp(d->comments[k].author, "Author")) { n++; break; }
  for (k = 0; k < d->nrevs; k++) if (d->revs[k].author[0] && strcmp(d->revs[k].author, "Author")) { n++; break; }
  return n;
}
static int story_has(const Story *s) { u32 i; for (i = 0; i < s->np; i++) if (s->p[i]->len) return 1; return 0; }
void inspect_doc(Doc *d, InspectReport *r)
{
  u32 i, k;
  int c;
  Pos at;
  memset(r, 0, sizeof *r);
  for (c = 0; c < d->ncomments; c++) if (doc_comment_mark(d, c, 0, &at)) r->comments++;
  for (i = 0; i < d->np; i++)
    for (k = 0; k < d->p[i]->nr; k++) if (d->p[i]->r[k].f.rev != REV_NONE) r->revisions++;
  r->props = props_count(d);
  /* (hidden text: Quill has none of its own yet; WP9 adds it) */
  for (c = 0; c < HF_N; c++) { if (story_has(&d->hdr[c])) r->headers++; if (story_has(&d->ftr[c])) r->headers++; }
  for (c = 0; c < d->nsects; c++) if (d->sects[c].hf) for (k = 0; k < 2 * HF_N; k++) if (story_has(&d->sects[c].hf[k])) r->headers++;
  if (d->wm.text[0]) r->headers++;
  for (c = 0; c < d->nkept; c++) if (!strncmp(d->kept[c].name, "customXml/item", 14) && !strstr(d->kept[c].name, "Props") && !strstr(d->kept[c].name, ".rels")) r->customxml++;
}
unsigned inspect_remove(Doc *d, unsigned what)
{
  unsigned done = 0;
  int c;
  u32 k;
  Pos at;
  if (what & INS_COMMENTS) {
    for (c = 0; c < d->ncomments; c++) if (doc_comment_mark(d, c, 0, &at)) { doc_delete_comment(d, c); done |= INS_COMMENTS; }
  }
  if ((what & INS_REVISIONS) && d->np) {
    Pos a, b;
    a.para = 0; a.off = 0; b = doc_end_pos(d);
    if (doc_resolve(d, a, b, 1)) done |= INS_REVISIONS;
  }
  if (what & INS_PROPS) {
    static const int which[] = { IP_SUBJECT, IP_KEYWORDS, IP_DESCRIPTION, IP_CATEGORY, IP_STATUS, IP_LASTBY, IP_COMPANY, IP_MANAGER, IP_LASTPRINTED };
    d->author[0] = 0;
    for (k = 0; k < sizeof which / sizeof which[0]; k++) info_set(d, which[k], NULL);
    while (d->info && d->info->ncust) info_cust_remove(d, d->info->ncust - 1);
    for (c = 0; c < d->ncomments; c++) { strcpy(d->comments[c].author, "Author"); d->comments[c].initials[0] = 0; }
    for (c = 0; c < d->nrevs; c++) strcpy(d->revs[c].author, "Author");
    done |= INS_PROPS;
  }
  if (what & INS_HEADERS) {
    for (c = 0; c < HF_N; c++) { story_free(&d->hdr[c]); story_free(&d->ftr[c]); }
    for (c = 0; c < d->nsects; c++) if (d->sects[c].hf) for (k = 0; k < 2 * HF_N; k++) story_free(&d->sects[c].hf[k]);
    d->wm.text[0] = 0;
    done |= INS_HEADERS;
  }
  if (what & INS_CUSTOMXML) {
    for (c = d->nkept - 1; c >= 0; c--) if (!strncmp(d->kept[c].name, "customXml/", 10)) keep_remove(d, d->kept[c].name);
    done |= INS_CUSTOMXML;
  }
  if (done) { d->dirty = 1; d->ver++; for (k = 0; k < d->np; k++) d->p[k]->ver++; }
  return done;
}

/* ---- Check Accessibility */
static const char *const msgs[AR_N] = {
  "Missing alternative text", "No header row specified", "Heading level skipped (Heading %d after Heading %d)", "Hard to read text contrast",
  "Object not in line with text", "Merged or split cells", "%d empty paragraphs in a row", "Link text isn't descriptive", "Document title missing" };
static const char *const fixes[AR_N] = {
  "Add a description, or mark it as decorative", "Mark the first row as a header row (Table Layout: Header Row)",
  "Use the next heading level down, so the outline has no gaps", "Use a darker or lighter text colour, or change the shading behind it",
  "Place it in line with text (Wrap Text: In Line with Text)", "Split merged cells, so screen readers can follow the table",
  "Use space before and after paragraphs instead of empty ones", "Use words that say where the link goes", "Give the document a title (File: Info: Properties)" };
const char *acc_message(int rule) { return rule >= 0 && rule < AR_N ? msgs[rule] : ""; }
const char *acc_fix(int rule) { return rule >= 0 && rule < AR_N ? fixes[rule] : ""; }
/* sRGB channel values made linear, times 100000 (WCAG's relative luminance) */
static const u32 lin[256] = {
0, 30, 61, 91, 121, 152, 182, 212, 243, 273, 304, 335, 368, 402, 439, 478,
518, 561, 605, 651, 700, 750, 802, 857, 913, 972, 1033, 1096, 1161, 1229, 1298, 1370,
1444, 1521, 1600, 1681, 1764, 1850, 1938, 2029, 2122, 2217, 2315, 2416, 2519, 2624, 2732, 2843,
2956, 3071, 3190, 3310, 3434, 3560, 3689, 3820, 3955, 4092, 4231, 4374, 4519, 4667, 4817, 4971,
5127, 5286, 5448, 5613, 5781, 5951, 6125, 6301, 6480, 6663, 6848, 7036, 7227, 7421, 7619, 7819,
8022, 8228, 8438, 8650, 8866, 9084, 9306, 9531, 9759, 9990, 10224, 10462, 10702, 10946, 11193, 11444,
11697, 11954, 12214, 12477, 12744, 13014, 13287, 13563, 13843, 14126, 14413, 14703, 14996, 15293, 15593, 15896,
16203, 16513, 16827, 17144, 17465, 17789, 18116, 18447, 18782, 19120, 19462, 19807, 20156, 20508, 20864, 21223,
21586, 21953, 22323, 22697, 23074, 23455, 23840, 24228, 24620, 25016, 25415, 25818, 26225, 26636, 27050, 27468,
27889, 28315, 28744, 29177, 29614, 30054, 30499, 30947, 31399, 31855, 32314, 32778, 33245, 33716, 34191, 34670,
35153, 35640, 36131, 36625, 37124, 37626, 38133, 38643, 39157, 39676, 40198, 40724, 41254, 41789, 42327, 42869,
43415, 43966, 44520, 45079, 45641, 46208, 46778, 47353, 47932, 48515, 49102, 49693, 50289, 50888, 51492, 52100,
52712, 53328, 53948, 54572, 55201, 55834, 56471, 57112, 57758, 58408, 59062, 59720, 60383, 61050, 61721, 62396,
63076, 63760, 64448, 65141, 65837, 66539, 67244, 67954, 68669, 69387, 70110, 70838, 71569, 72306, 73046, 73791,
74540, 75294, 76052, 76815, 77582, 78354, 79130, 79910, 80695, 81485, 82279, 83077, 83880, 84687, 85499, 86316,
87137, 87962, 88792, 89627, 90466, 91310, 92158, 93011, 93869, 94731, 95597, 96469, 97345, 98225, 99110, 100000
};
static u32 lum(u32 c) { return (2126 * lin[(c >> 16) & 255] + 7152 * lin[(c >> 8) & 255] + 722 * lin[c & 255]) / 10000; }
int acc_contrast(u32 fg, u32 bg)
{
  u32 a = lum(fg & 0xFFFFFF), b = lum(bg & 0xFFFFFF), t;
  if (a < b) { t = a; a = b; b = t; }
  return (int)((a + 5000) * 100 / (b + 5000));
}
static int heading_level(const Doc *d, const Para *p)
{
  const Style *st = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0];
  if (!strncmp(st->name, "heading ", 8) || !strncmp(st->name, "Heading ", 8)) return st->name[8] >= '1' && st->name[8] <= '9' ? st->name[8] - '0' : 1;
  return 0;
}
typedef struct { AccIssue *out; int max, n; } Acc;
static void add(Acc *a, int kind, int rule, u32 para, u32 off, int obj, int x, int y)
{
  AccIssue *i;
  if (a->n >= a->max) return;
  i = &a->out[a->n++];
  i->kind = (u8)kind; i->rule = (u8)rule; i->at.para = para; i->at.off = off; i->obj = obj; i->a = x; i->b = y;
}
static int blankp(const Para *p)
{
  u32 k;
  for (k = 0; k < p->len; k++) if (p->t[k] != ' ' && p->t[k] != '\t') return 0;
  return 1;
}
int acc_check(Doc *d, AccIssue *out, int max)
{
  AccIssue *all;
  Acc a;
  u32 i, k, blank0 = 0, nblank = 0;
  int last = 0, kind, n = 0, t;
  u8 *tseen;
  if (max <= 0) return 0;
  if (!(all = (AccIssue *)malloc((size_t)max * 3 * sizeof(AccIssue)))) return 0;
  a.out = all; a.max = max * 3; a.n = 0;
  tseen = d->ntables ? (u8 *)calloc((size_t)d->ntables, 1) : NULL;
  if (!d->title[0]) add(&a, AK_TIP, AR_TITLE, 0, 0, -1, 0, 0);
  for (i = 0; i < d->np; i++) {
    const Para *p = d->p[i];
    int lv = heading_level(d, p);
    u32 r, start = 0;
    u32 bg = d->pagecolor ? d->pagecolor & 0xFFFFFF : 0xFFFFFF;
    /* empty paragraphs in a row: spacing made with Return */
    if (!in_table(&p->pf) && blankp(p) && !p->pf.sect) { if (!nblank++) blank0 = i; }
    else { if (nblank >= 3) add(&a, AK_TIP, AR_BLANK, blank0, 0, -1, (int)nblank, 0); nblank = 0; }
    /* headings one level at a time */
    if (lv && p->len) { if (last && lv > last + 1) add(&a, AK_WARNING, AR_HEADINGORDER, i, 0, -1, lv, last); last = lv; }
    /* tables: a header row; merged cells */
    if (in_table(&p->pf) && tseen && p->pf.cell.table <= d->ntables && !tseen[p->pf.cell.table - 1]) {
      u32 j;
      int head = 0, merged = 0;
      u16 tb = p->pf.cell.table;
      tseen[tb - 1] = 1;
      for (j = i; j < d->np && d->p[j]->pf.cell.table == tb; j++) {
        const Cell *c = &d->p[j]->pf.cell;
        if (c->row == 0 && c->header) head = 1;
        if (c->span > 1 || c->vmerge) merged = 1;
      }
      if (!head) add(&a, AK_WARNING, AR_TABLEHEADER, i, 0, -1, 0, 0);
      if (merged) add(&a, AK_TIP, AR_MERGED, i, 0, -1, 0, 0);
    }
    if (p->pf.shade) bg = p->pf.shade & 0xFFFFFF;
    for (r = 0; r < p->nr; r++) {
      const CharFmt *f = &p->r[r].f;
      u32 e = p->r[r].end;
      /* contrast: text against what is behind it (automatic colour is always readable) */
      if (e > start && f->color != COLOR_AUTO && !blankp(p)) {
        u32 back = f->hl ? f->hl & 0xFFFFFF : bg;
        int large = f->size >= 36 || (f->size >= 28 && (f->flags & CF_BOLD));
        int c = acc_contrast(f->color, back);
        if (c < (large ? 300 : 450)) { add(&a, AK_WARNING, AR_CONTRAST, i, start, -1, c, 0); break; }
      }
      start = e;
    }
    /* objects: alternative text; floating ones */
    for (k = 0; k + 4 <= p->len; k++) {
      int o;
      const Image *im;
      if (!IS_PIC(p->t + k)) continue;
      o = pic_index(p->t + k);
      if (o < d->nimages) {
        im = &d->images[o];
        if (im->kind != OBJ_TEXTBOX && !im->alt[0] && !im->decorative) add(&a, AK_ERROR, AR_ALTTEXT, i, k, o, 0, 0);
        if (im->wrap != WRAP_INLINE && !im->decorative) add(&a, AK_TIP, AR_FLOATING, i, k, o, 0, 0);
      }
      k += 3;
    }
    /* links that say nothing of where they go */
    for (r = 0, start = 0; r < p->nr; r++) {
      u32 e = p->r[r].end, s = start;
      start = e;
      if (p->r[r].f.link && e > s) {
        char w[24];
        u32 nlen = e - s, j;
        while (r + 1 < p->nr && p->r[r + 1].f.link == p->r[r].f.link) { r++; nlen = p->r[r].end - s; start = p->r[r].end; }
        if (nlen < sizeof w) {
          memcpy(w, p->t + s, nlen); w[nlen] = 0;
          for (j = 0; w[j]; j++) if (w[j] >= 'A' && w[j] <= 'Z') w[j] = (char)(w[j] + 32);
          if (!strcmp(w, "click here") || !strcmp(w, "here") || !strcmp(w, "link") || !strcmp(w, "more") || !strcmp(w, "read more"))
            add(&a, AK_TIP, AR_LINKTEXT, i, s, -1, 0, 0);
        }
      }
    }
  }
  if (nblank >= 3) add(&a, AK_TIP, AR_BLANK, blank0, 0, -1, (int)nblank, 0);
  free(tseen);
  /* errors, then warnings, then tips */
  for (kind = AK_ERROR; kind <= AK_TIP; kind++)
    for (t = 0; t < a.n && n < max; t++) if (all[t].kind == kind) out[n++] = all[t];
  free(all);
  return n;
}

/* ---- Check Compatibility */
int compat_check(Doc *d, CompatIssue *out, int max)
{
  u32 shapes = 0, decor = 0, wide = 0, beneath = 0, seps = 0, kept = 0, cust = 0;
  int k, n = 0;
  for (k = 0; k < d->nimages; k++) {
    if (d->images[k].kind != OBJ_PICTURE) shapes++;
    if (d->images[k].decorative) decor++;
  }
  for (k = 0; k < d->ntables; k++) if (d->tables[k].ncols > 63) wide++;
  if (d->fnpr.pos == NPOS_BENEATHTEXT && (d->fnpr.set & NPS_POS)) beneath++;
  if (d->nseps) seps++;
  for (k = 0; k < d->nkept; k++) if (!strstr(d->kept[k].name, ".rels")) kept++;
  if (d->info) cust = (u32)d->info->ncust;
#define ISSUE(c, m) do { if ((c) && n < max) { out[n].msg = (m); out[n].count = (c); n++; } } while (0)
  ISSUE(shapes, "Shapes and text boxes become pictures or simpler drawings in Word 97-2003 (%d)");
  ISSUE(decor, "Pictures marked decorative lose the mark in Word 2016 and earlier (%d)");
  ISSUE(wide, "Tables wider than 63 columns are cut short in Word 97-2003 (%d)");
  ISSUE(beneath, "Footnotes placed beneath the text go to the page's foot in Word 97-2003");
  ISSUE(seps, "The document's own footnote separators are shown as plain lines by other programs");
  ISSUE(cust, "Custom properties are kept, but Word 97-2003 shows only text ones (%d)");
  ISSUE(kept, "Parts of the file Quill doesn't show are kept as they are (%d)");
#undef ISSUE
  return n;
}
