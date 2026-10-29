/* zip.c - see zip.h */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zip.h"

/* ------------------------------------------------------------------ */
/* CRC-32 (zip's polynomial), table built on first use */
static u32 crctab[256];
/* add n bytes to a running CRC-32; pass 0 to start and the previous result to continue. The
   pre- and post-inversion are done here, so callers just chain the values. */
u32 crc32_update(u32 crc, const u8 *p, size_t n)
{
  size_t i;
  if (!crctab[1]) {
    u32 k, c;
    for (k = 0; k < 256; k++) {
      c = k;
      for (i = 0; i < 8; i++) c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
      crctab[k] = c;
    }
  }
  crc = ~crc;
  for (i = 0; i < n; i++) crc = crctab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

/* ------------------------------------------------------------------ */
/* inflate (RFC 1951) */
typedef struct { const u8 *in; size_t len, pos; u32 bits; int nbits; int err; } BitIn;
/* the next n bits (n <= 24), least significant first as deflate packs them. Running out of input
   sets b->err and returns 0, so callers can check once after a group of reads. */
static u32 getbits(BitIn *b, int n)
{
  u32 v;
  while (b->nbits < n) {
    if (b->pos >= b->len) { b->err = 1; return 0; }
    b->bits |= (u32)b->in[b->pos++] << b->nbits;
    b->nbits += 8;
  }
  v = b->bits & ((1UL << n) - 1);
  b->bits >>= n; b->nbits -= n;
  return v;
}
typedef struct { u16 count[16]; u16 sym[320]; } Huff;
/* build a canonical Huffman decoding table from n code lengths (0 = symbol unused): count[] holds
   how many codes there are of each length, sym[] the symbols in code order. Returns 0 if the
   lengths are over-subscribed (damaged data); incomplete codes are allowed, as zlib allows them. */
static int build(Huff *h, const u8 *lens, int n)
{
  u16 offs[16];
  int i, left = 1;
  memset(h->count, 0, sizeof h->count);
  for (i = 0; i < n; i++) h->count[lens[i]]++;
  h->count[0] = 0;
  for (i = 1; i < 16; i++) { left <<= 1; left -= h->count[i]; if (left < 0) return 0; }   /* over-subscribed */
  offs[1] = 0;
  for (i = 1; i < 15; i++) offs[i + 1] = (u16)(offs[i] + h->count[i]);
  for (i = 0; i < n; i++) if (lens[i]) h->sym[offs[lens[i]]++] = (u16)i;
  return 1;
}
/* read one symbol with table h, a bit at a time (the puff.c method: slow-ish but tiny and needs no
   lookup tables); -1 on running out of input or an invalid code */
static int decode(BitIn *b, const Huff *h)
{
  int code = 0, first = 0, index = 0, len;
  for (len = 1; len < 16; len++) {
    int count;
    code |= (int)getbits(b, 1);
    if (b->err) return -1;
    count = h->count[len];
    if (code - count < first) return h->sym[index + (code - first)];
    index += count; first += count;
    first <<= 1; code <<= 1;
  }
  return -1;
}
static const u16 lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const u8 lextra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const u16 dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                               4097, 6145, 8193, 12289, 16385, 24577 };
static const u8 dextra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
/* decode one compressed block's literals and length/distance pairs into out at *op, up to the
   end-of-block code. Returns 1 at the end of the block, 0 on damaged data, a distance reaching
   before the start of out, or output past cap. */
static int codes(BitIn *b, const Huff *lit, const Huff *dist, u8 *out, size_t cap, size_t *op)
{
  for (;;) {
    int s = decode(b, lit);
    if (s < 0) return 0;
    if (s < 256) { if (*op >= cap) return 0; out[(*op)++] = (u8)s; }
    else if (s == 256) return 1;
    else {
      u32 len, d;
      int ds;
      s -= 257;
      if (s >= 29) return 0;
      len = lbase[s] + getbits(b, lextra[s]);
      ds = decode(b, dist);
      if (ds < 0 || ds >= 30) return 0;
      d = dbase[ds] + getbits(b, dextra[ds]);
      if (b->err || d > *op || *op + len > cap) return 0;
      while (len--) { out[*op] = out[*op - d]; (*op)++; }
    }
  }
}
/* inflate raw deflate data (no zlib or gzip header) block by block: stored, fixed or dynamic
   Huffman. The output size must be known in advance (zip records it), so there is no growing
   buffer; returns the bytes written or -1 if damaged or too big for cap. */
long zinflate(const u8 *in, size_t inlen, u8 *out, size_t cap)
{
  BitIn b;
  size_t op = 0;
  int last;
  Huff lit, dist;
  memset(&b, 0, sizeof b);
  b.in = in; b.len = inlen;
  do {
    int type;
    last = (int)getbits(&b, 1);
    type = (int)getbits(&b, 2);
    if (b.err) return -1;
    if (type == 0) {                      /* stored */
      u32 n, nn;
      b.bits = 0; b.nbits = 0;            /* to a byte boundary */
      if (b.pos + 4 > b.len) return -1;
      n = in[b.pos] | ((u32)in[b.pos + 1] << 8); nn = in[b.pos + 2] | ((u32)in[b.pos + 3] << 8);
      b.pos += 4;
      if ((n ^ 0xFFFF) != nn || b.pos + n > b.len || op + n > cap) return -1;
      memcpy(out + op, in + b.pos, n);
      op += n; b.pos += n;
    } else if (type == 1) {               /* fixed codes */
      u8 lens[320];
      int i;
      for (i = 0; i < 144; i++) lens[i] = 8;
      for (; i < 256; i++) lens[i] = 9;
      for (; i < 280; i++) lens[i] = 7;
      for (; i < 288; i++) lens[i] = 8;
      build(&lit, lens, 288);
      for (i = 0; i < 30; i++) lens[i] = 5;
      build(&dist, lens, 30);
      if (!codes(&b, &lit, &dist, out, cap, &op)) return -1;
    } else if (type == 2) {               /* dynamic codes */
      static const u8 order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
      u8 lens[320];
      Huff cl;
      int nlen = (int)getbits(&b, 5) + 257, ndist = (int)getbits(&b, 5) + 1, ncode = (int)getbits(&b, 4) + 4, i;
      if (b.err || nlen > 286 || ndist > 30) return -1;
      memset(lens, 0, sizeof lens);
      for (i = 0; i < ncode; i++) lens[order[i]] = (u8)getbits(&b, 3);
      if (!build(&cl, lens, 19)) return -1;
      for (i = 0; i < nlen + ndist;) {
        int s = decode(&b, &cl), rep = 0, val = 0;
        if (s < 0) return -1;
        if (s < 16) { lens[i++] = (u8)s; continue; }
        if (s == 16) { if (!i) return -1; val = lens[i - 1]; rep = 3 + (int)getbits(&b, 2); }
        else if (s == 17) rep = 3 + (int)getbits(&b, 3);
        else rep = 11 + (int)getbits(&b, 7);
        if (b.err || i + rep > nlen + ndist) return -1;
        while (rep--) lens[i++] = (u8)val;
      }
      if (!lens[256] || !build(&lit, lens, nlen) || !build(&dist, lens + nlen, ndist)) return -1;
      if (!codes(&b, &lit, &dist, out, cap, &op)) return -1;
    } else return -1;
  } while (!last);
  return (long)op;
}

/* ------------------------------------------------------------------ */
/* deflate: greedy LZ77 over a 32 KB window with hash chains, fixed Huffman codes. XML squeezes
   well even so; it keeps the code small and quick on a 68040 */
typedef struct { Buf *o; u32 bits; int n; } BitOut;
/* append the low n bits of v, least significant first, flushing whole bytes to the buffer */
static void putbits(BitOut *b, u32 v, int n)
{
  b->bits |= v << b->n; b->n += n;
  while (b->n >= 8) { buf_addc(b->o, (int)(b->bits & 0xFF)); b->bits >>= 8; b->n -= 8; }
}
static void putrev(BitOut *b, u32 code, int n)        /* Huffman codes go most significant bit first */
{
  u32 r = 0;
  int i;
  for (i = 0; i < n; i++) { r = (r << 1) | (code & 1); code >>= 1; }
  putbits(b, r, n);
}
/* write literal/length symbol c (0..287) with deflate's fixed Huffman code (RFC 1951 3.2.6) */
static void lit(BitOut *b, int c)
{
  if (c < 144) putrev(b, 0x30 + c, 8);
  else if (c < 256) putrev(b, 0x190 + c - 144, 9);
  else if (c < 280) putrev(b, c - 256, 7);
  else putrev(b, 0xC0 + c - 280, 8);
}
/* write a back-reference: len 3..258 bytes from dist 1..32768 back, as a length symbol with its
   extra bits, then a fixed 5-bit distance code with its extra bits */
static void match(BitOut *b, u32 len, u32 dist)
{
  int s = 28, ds = 29;
  while (s > 0 && lbase[s] > len) s--;
  lit(b, 257 + s);
  putbits(b, len - lbase[s], lextra[s]);
  while (ds > 0 && dbase[ds] > dist) ds--;
  putrev(b, (u32)ds, 5);
  putbits(b, dist - dbase[ds], dextra[ds]);
}
#define HBITS 14
/* compress in[0..n) as a single fixed-code deflate block. head[] maps a hash of the next three
   bytes to the latest position seen, prev[] chains back to earlier ones; up to 32 candidates are
   tried per position and the longest match is taken. Returns a malloc'd buffer (caller frees),
   *outlen its size; NULL if out of memory. */
u8 *zdeflate(const u8 *in, size_t n, size_t *outlen)
{
  Buf o;
  BitOut b;
  i32 *head = (i32 *)malloc(sizeof(i32) << HBITS), *prev = (i32 *)malloc(sizeof(i32) * 32768);
  size_t i = 0;
  if (!head || !prev) { free(head); free(prev); return NULL; }
  memset(head, 0xFF, sizeof(i32) << HBITS);
  buf_init(&o);
  b.o = &o; b.bits = 0; b.n = 0;
  putbits(&b, 1, 1); putbits(&b, 1, 2);          /* one final block, fixed codes */
  while (i < n) {
    u32 bestlen = 0, bestd = 0;
    if (i + 3 <= n) {
      u32 h = ((u32)in[i] << 10 ^ (u32)in[i + 1] << 5 ^ in[i + 2]) & ((1 << HBITS) - 1);
      i32 c = head[h];
      int chain = 32;
      while (c >= 0 && i - (size_t)c <= 32768 && chain--) {
        u32 l = 0, max = n - i < 258 ? (u32)(n - i) : 258;
        while (l < max && in[c + l] == in[i + l]) l++;
        if (l > bestlen) { bestlen = l; bestd = (u32)(i - (size_t)c); if (l == max) break; }
        c = prev[c & 32767];
      }
      prev[i & 32767] = head[h]; head[h] = (i32)i;
    }
    if (bestlen >= 3) {
      size_t k;
      match(&b, bestlen, bestd);
      for (k = 1; k < bestlen && i + k + 3 <= n; k++) {     /* index the matched bytes too */
        u32 h = ((u32)in[i + k] << 10 ^ (u32)in[i + k + 1] << 5 ^ in[i + k + 2]) & ((1 << HBITS) - 1);
        prev[(i + k) & 32767] = head[h]; head[h] = (i32)(i + k);
      }
      i += bestlen;
    } else lit(&b, in[i++]);
  }
  lit(&b, 256);
  if (b.n) putbits(&b, 0, 8 - b.n);
  free(head); free(prev);
  if (o.oom) { buf_free(&o); return NULL; }
  *outlen = o.len;
  return (u8 *)buf_take(&o);
}

/* ------------------------------------------------------------------ */
/* reading */
/* little-endian 16- and 32-bit fields, as zip stores them (and the 68k is big-endian) */
static u32 le16(const u8 *p) { return p[0] | ((u32)p[1] << 8); }
static u32 le32(const u8 *p) { return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
/* find the end-of-central-directory record (searching back over a zip comment of up to 64 KB) and
   read the central directory into z->e. data is not copied and must outlive the reader. Entries
   after a damaged one are dropped; zip64 is not supported (no document needs it). Returns 0 if it
   is not a zip or memory runs out; otherwise call zip_close. */
int zip_open(ZipReader *z, const u8 *data, size_t len)
{
  size_t eocd = 0, i, p;
  u32 n, cdoff, cdsize, k;
  memset(z, 0, sizeof *z);
  if (len < 22) return 0;
  for (i = len - 22 + 1; i-- > 0 && len - i < 22 + 65536;)
    if (le32(data + i) == 0x06054B50UL) { eocd = i + 1; break; }
  if (!eocd--) return 0;
  n = le16(data + eocd + 10);
  cdsize = le32(data + eocd + 12); cdoff = le32(data + eocd + 16);
  if (cdoff > len || cdsize > len - cdoff || n > 10000) return 0;
  if (!(z->e = (ZipEntry *)calloc(n ? n : 1, sizeof(ZipEntry)))) return 0;
  z->data = data; z->len = len;
  for (k = 0, p = cdoff; k < n; k++) {
    ZipEntry *e = &z->e[z->n];
    u32 nl, xl, cl;
    if (p + 46 > len || le32(data + p) != 0x02014B50UL) break;
    nl = le16(data + p + 28); xl = le16(data + p + 30); cl = le16(data + p + 32);
    if (p + 46 + nl > len) break;
    e->method = le16(data + p + 10); e->crc = le32(data + p + 16);
    e->csize = le32(data + p + 20); e->usize = le32(data + p + 24); e->local = le32(data + p + 42);
    memcpy(e->name, data + p + 46, nl < 255 ? nl : 255);
    e->name[nl < 255 ? nl : 255] = 0;
    z->n++;
    p += 46 + nl + xl + cl;
  }
  return 1;
}
/* free the entry list (the archive data stays the caller's) */
void zip_close(ZipReader *z) { free(z->e); memset(z, 0, sizeof *z); }
/* the index of the entry with this path, compared case-insensitively since some writers vary the
   case of names like "[Content_Types].xml"; -1 if absent */
int zip_find(ZipReader *z, const char *name)
{
  int i;
  for (i = 0; i < z->n; i++) if (str_ieq(z->e[i].name, name)) return i;
  return -1;
}
/* entry i's contents, malloc'd with a NUL after them so XML can be parsed in place; *len (if not
   NULL) gets the size without the NUL. The sizes come from the central directory, since the local
   header may defer them to a data descriptor. NULL if i is out of range, the entry is damaged, its
   CRC is wrong, it uses a method other than stored or deflate, or it is over 64 MB. */
u8 *zip_read(ZipReader *z, int i, size_t *len)
{
  ZipEntry *e;
  size_t p;
  u8 *out;
  if (i < 0 || i >= z->n) return NULL;
  e = &z->e[i];
  p = e->local;
  if (p + 30 > z->len || le32(z->data + p) != 0x04034B50UL) return NULL;
  p += 30 + le16(z->data + p + 26) + le16(z->data + p + 28);
  if (p > z->len || e->csize > z->len - p || e->usize > 64UL * 1024 * 1024) return NULL;
  if (!(out = (u8 *)malloc(e->usize + 1))) return NULL;
  if (e->method == 0) {
    if (e->csize != e->usize) { free(out); return NULL; }
    memcpy(out, z->data + p, e->usize);
  } else if (e->method == 8) {
    if (zinflate(z->data + p, e->csize, out, e->usize) != (long)e->usize) { free(out); return NULL; }
  } else { free(out); return NULL; }
  if (crc32_update(0, out, e->usize) != e->crc) { free(out); return NULL; }
  out[e->usize] = 0;
  if (len) *len = e->usize;
  return out;
}

/* ------------------------------------------------------------------ */
/* writing */
/* append little-endian 16- and 32-bit fields */
static void w16(Buf *b, u32 v) { buf_addc(b, (int)(v & 0xFF)); buf_addc(b, (int)((v >> 8) & 0xFF)); }
static void w32(Buf *b, u32 v) { w16(b, v & 0xFFFF); w16(b, v >> 16); }
/* start an empty archive. Entries are stamped with the current local time in DOS format; a clock
   before 1980 (an Amiga with no battery clock) gets 1 January 2026 instead, as DOS dates can't go
   earlier. */
void zip_begin(ZipWriter *w)
{
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  buf_init(&w->out); buf_init(&w->cdir); w->n = 0;
  if (tm && tm->tm_year >= 80) {
    w->dostime = (u16)((tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2));
    w->dosdate = (u16)(((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday);
  } else { w->dostime = 0; w->dosdate = (u16)((46 << 9) | (1 << 5) | 1); }
}
/* append a file: the local header and data go straight into w->out, its central directory record
   into w->cdir for zip_finish. Deflated, or stored if that would not be smaller. The name is
   written as given (use '/' between folders). Returns 0 if out of memory. */
int zip_add(ZipWriter *w, const char *name, const void *data, size_t len)
{
  size_t clen = 0, nl = strlen(name);
  u8 *c = zdeflate((const u8 *)data, len, &clen);
  u32 crc = crc32_update(0, (const u8 *)data, len), off = (u32)w->out.len;
  int method = 8;
  if (!c) return 0;
  if (clen >= len) { free(c); c = NULL; method = 0; clen = len; }      /* not worth it: stored */
  w32(&w->out, 0x04034B50UL); w16(&w->out, 20); w16(&w->out, 0); w16(&w->out, (u32)method);
  w16(&w->out, w->dostime); w16(&w->out, w->dosdate); w32(&w->out, crc); w32(&w->out, (u32)clen); w32(&w->out, (u32)len);
  w16(&w->out, (u32)nl); w16(&w->out, 0);
  buf_add(&w->out, name, nl);
  buf_add(&w->out, c ? (const void *)c : data, clen);
  free(c);
  w32(&w->cdir, 0x02014B50UL); w16(&w->cdir, 20); w16(&w->cdir, 20); w16(&w->cdir, 0); w16(&w->cdir, (u32)method);
  w16(&w->cdir, w->dostime); w16(&w->cdir, w->dosdate); w32(&w->cdir, crc); w32(&w->cdir, (u32)clen); w32(&w->cdir, (u32)len);
  w16(&w->cdir, (u32)nl); w16(&w->cdir, 0); w16(&w->cdir, 0); w16(&w->cdir, 0); w16(&w->cdir, 0); w32(&w->cdir, 0); w32(&w->cdir, off);
  buf_add(&w->cdir, name, nl);
  w->n++;
  return !w->out.oom && !w->cdir.oom;
}
/* append the central directory and end record; the finished archive is w->out.data (w->out.len
   bytes), still owned by w. Returns 0 if out of memory. */
int zip_finish(ZipWriter *w)
{
  u32 off = (u32)w->out.len;
  buf_add(&w->out, w->cdir.data, w->cdir.len);
  w32(&w->out, 0x06054B50UL); w16(&w->out, 0); w16(&w->out, 0); w16(&w->out, (u32)w->n); w16(&w->out, (u32)w->n);
  w32(&w->out, (u32)w->cdir.len); w32(&w->out, off); w16(&w->out, 0);
  return !w->out.oom;
}
/* free both buffers (take w->out first with buf_take to keep the archive) */
void zip_free(ZipWriter *w) { buf_free(&w->out); buf_free(&w->cdir); }
