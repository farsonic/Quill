/* mathtex.c - WP14b: equations in LaTeX (the Equation tab's LaTeX input and Linear form). LaTeX is
   read by turning it into UnicodeMath linear text (which math.c builds up), and the tree is written
   straight out as LaTeX. See mathedit.h. Written from the LaTeX and amsmath manuals */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mathedit.h"

/* ------------------------------------------------------------------ */
/* LaTeX's names for characters (first match wins when writing) */
typedef struct { const char *name; u32 cp; } TName;
static const TName tn[] = {
  { "alpha", 0x3B1 }, { "beta", 0x3B2 }, { "gamma", 0x3B3 }, { "delta", 0x3B4 }, { "epsilon", 0x3F5 }, { "varepsilon", 0x3B5 },
  { "zeta", 0x3B6 }, { "eta", 0x3B7 }, { "theta", 0x3B8 }, { "vartheta", 0x3D1 }, { "iota", 0x3B9 }, { "kappa", 0x3BA },
  { "lambda", 0x3BB }, { "mu", 0x3BC }, { "nu", 0x3BD }, { "xi", 0x3BE }, { "pi", 0x3C0 }, { "varpi", 0x3D6 }, { "rho", 0x3C1 },
  { "varrho", 0x3F1 }, { "sigma", 0x3C3 }, { "varsigma", 0x3C2 }, { "tau", 0x3C4 }, { "upsilon", 0x3C5 }, { "phi", 0x3D5 },
  { "varphi", 0x3C6 }, { "chi", 0x3C7 }, { "psi", 0x3C8 }, { "omega", 0x3C9 },
  { "Gamma", 0x393 }, { "Delta", 0x394 }, { "Theta", 0x398 }, { "Lambda", 0x39B }, { "Xi", 0x39E }, { "Pi", 0x3A0 }, { "Sigma", 0x3A3 },
  { "Upsilon", 0x3A5 }, { "Phi", 0x3A6 }, { "Psi", 0x3A8 }, { "Omega", 0x3A9 },
  { "sum", 0x2211 }, { "prod", 0x220F }, { "coprod", 0x2210 }, { "int", 0x222B }, { "iint", 0x222C }, { "iiint", 0x222D }, { "oint", 0x222E },
  { "oiint", 0x222F }, { "oiiint", 0x2230 }, { "bigcup", 0x22C3 }, { "bigcap", 0x22C2 }, { "bigvee", 0x22C1 }, { "bigwedge", 0x22C0 },
  { "bigoplus", 0x2A01 }, { "bigotimes", 0x2A02 }, { "bigodot", 0x2A00 }, { "biguplus", 0x2A04 }, { "bigsqcup", 0x2A06 },
  { "pm", 0xB1 }, { "mp", 0x2213 }, { "times", 0xD7 }, { "div", 0xF7 }, { "cdot", 0x22C5 }, { "ast", 0x2217 }, { "star", 0x22C6 },
  { "circ", 0x2218 }, { "bullet", 0x2219 }, { "oplus", 0x2295 }, { "ominus", 0x2296 }, { "otimes", 0x2297 }, { "oslash", 0x2298 },
  { "odot", 0x2299 }, { "cup", 0x222A }, { "cap", 0x2229 }, { "uplus", 0x228E }, { "sqcup", 0x2294 }, { "sqcap", 0x2293 },
  { "vee", 0x2228 }, { "lor", 0x2228 }, { "wedge", 0x2227 }, { "land", 0x2227 }, { "setminus", 0x2216 }, { "wr", 0x2240 },
  { "leq", 0x2264 }, { "le", 0x2264 }, { "geq", 0x2265 }, { "ge", 0x2265 }, { "neq", 0x2260 }, { "ne", 0x2260 }, { "ll", 0x226A },
  { "gg", 0x226B }, { "approx", 0x2248 }, { "equiv", 0x2261 }, { "sim", 0x223C }, { "simeq", 0x2243 }, { "cong", 0x2245 },
  { "propto", 0x221D }, { "prec", 0x227A }, { "succ", 0x227B }, { "preceq", 0x2AAF }, { "succeq", 0x2AB0 }, { "subset", 0x2282 },
  { "supset", 0x2283 }, { "subseteq", 0x2286 }, { "supseteq", 0x2287 }, { "in", 0x2208 }, { "notin", 0x2209 }, { "ni", 0x220B },
  { "perp", 0x22A5 }, { "parallel", 0x2225 }, { "mid", 0x2223 }, { "vdash", 0x22A2 }, { "dashv", 0x22A3 }, { "models", 0x22A8 },
  { "doteq", 0x2250 }, { "asymp", 0x224D }, { "bowtie", 0x22C8 }, { "nleq", 0x2270 }, { "ngeq", 0x2271 }, { "nless", 0x226E },
  { "ngtr", 0x226F }, { "nsubseteq", 0x2288 }, { "nsupseteq", 0x2289 },
  { "rightarrow", 0x2192 }, { "to", 0x2192 }, { "leftarrow", 0x2190 }, { "gets", 0x2190 }, { "uparrow", 0x2191 }, { "downarrow", 0x2193 },
  { "leftrightarrow", 0x2194 }, { "updownarrow", 0x2195 }, { "Rightarrow", 0x21D2 }, { "Leftarrow", 0x21D0 }, { "Uparrow", 0x21D1 },
  { "Downarrow", 0x21D3 }, { "Leftrightarrow", 0x21D4 }, { "longrightarrow", 0x27F6 }, { "longleftarrow", 0x27F5 },
  { "longleftrightarrow", 0x27F7 }, { "Longrightarrow", 0x27F9 }, { "Longleftarrow", 0x27F8 }, { "Longleftrightarrow", 0x27FA },
  { "implies", 0x27F9 }, { "iff", 0x27FA }, { "mapsto", 0x21A6 }, { "longmapsto", 0x27FC }, { "hookleftarrow", 0x21A9 },
  { "hookrightarrow", 0x21AA }, { "nearrow", 0x2197 }, { "searrow", 0x2198 }, { "swarrow", 0x2199 }, { "nwarrow", 0x2196 },
  { "rightleftharpoons", 0x21CC }, { "leftharpoonup", 0x21BC }, { "rightharpoonup", 0x21C0 },
  { "infty", 0x221E }, { "partial", 0x2202 }, { "nabla", 0x2207 }, { "forall", 0x2200 }, { "exists", 0x2203 }, { "nexists", 0x2204 },
  { "emptyset", 0x2205 }, { "varnothing", 0x2205 }, { "neg", 0xAC }, { "lnot", 0xAC }, { "angle", 0x2220 }, { "triangle", 0x25B3 },
  { "square", 0x25A1 }, { "prime", 0x2032 }, { "dagger", 0x2020 }, { "ddagger", 0x2021 }, { "ell", 0x2113 }, { "hbar", 0x210F },
  { "Re", 0x211C }, { "Im", 0x2111 }, { "aleph", 0x2135 }, { "beth", 0x2136 }, { "wp", 0x2118 }, { "therefore", 0x2234 },
  { "because", 0x2235 }, { "ldots", 0x2026 }, { "dots", 0x2026 }, { "cdots", 0x22EF }, { "vdots", 0x22EE }, { "ddots", 0x22F1 },
  { "degree", 0xB0 }, { "complement", 0x2201 }, { "top", 0x22A4 }, { "bot", 0x22A5 }, { "diamond", 0x22C4 }, { "S", 0xA7 },
  { "langle", 0x27E8 }, { "rangle", 0x27E9 }, { "lfloor", 0x230A }, { "rfloor", 0x230B }, { "lceil", 0x2308 }, { "rceil", 0x2309 },
  { "lbrack", '[' }, { "rbrack", ']' }, { "vert", '|' }, { "Vert", 0x2016 }, { "lvert", '|' }, { "rvert", '|' }, { "lVert", 0x2016 },
  { "rVert", 0x2016 }, { "colon", ':' }, { "quad", 0x2003 }, { "qquad", 0x2003 }, { "triangleq", 0x225C }, { "coloneqq", 0x2254 },
  { "eqqcolon", 0x2255 }, { "boxdot", 0x22A1 }, { "boxplus", 0x229E }, { "boxminus", 0x229F }, { "boxtimes", 0x22A0 },
  { NULL, 0 }
};
static u32 tex_cp(const char *name, size_t n)
{
  int k;
  for (k = 0; tn[k].name; k++) if (strlen(tn[k].name) == n && !memcmp(tn[k].name, name, n)) return tn[k].cp;
  return 0;
}
static const char *tex_name(u32 cp)
{
  int k;
  for (k = 0; tn[k].name; k++) if (tn[k].cp == cp) return tn[k].name;
  return NULL;
}
static const char *const tex_funcs[] = { "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "coth", "sin", "cos", "tan", "cot", "sec", "csc",
                                         "log", "ln", "lg", "exp", "lim", "liminf", "limsup", "max", "min", "det", "arg", "deg", "dim", "gcd",
                                         "hom", "inf", "ker", "sup", "Pr", NULL };
/* accents: LaTeX's command, the combining character */
static const TName tacc[] = { { "hat", 0x302 }, { "widehat", 0x302 }, { "check", 0x30C }, { "tilde", 0x303 }, { "widetilde", 0x303 },
                              { "acute", 0x301 }, { "grave", 0x300 }, { "dot", 0x307 }, { "ddot", 0x308 }, { "dddot", 0x20DB },
                              { "breve", 0x306 }, { "bar", 0x305 }, { "vec", 0x20D7 }, { "overrightarrow", 0x20D7 },
                              { "overleftarrow", 0x20D6 }, { "overleftrightarrow", 0x20E1 }, { NULL, 0 } };

/* ------------------------------------------------------------------ */
/* LaTeX in: to linear text */
typedef struct { const char *s; size_t i, n; int depth; } Tx;
static int tx_peek(Tx *t) { return t->i < t->n ? (u8)t->s[t->i] : -1; }
static void put_u(Buf *o, u32 c) { char u[4]; buf_add(o, u, (size_t)utf8_put(u, c)); }
static void skip_ws(Tx *t) { while (t->i < t->n && (t->s[t->i] == ' ' || t->s[t->i] == '\t' || t->s[t->i] == '\n' || t->s[t->i] == '\r')) t->i++; }
/* a command's name after the backslash (letters, or one other character) */
static size_t cmd_name(Tx *t, const char **name)
{
  size_t a = t->i, b = a;
  if (b >= t->n) { *name = t->s + a; return 0; }
  if ((t->s[b] | 32) >= 'a' && (t->s[b] | 32) <= 'z') { while (b < t->n && (t->s[b] | 32) >= 'a' && (t->s[b] | 32) <= 'z') b++; }
  else b++;
  *name = t->s + a;
  t->i = b;
  return b - a;
}
static int is_cmd(const char *name, size_t n, const char *w) { return strlen(w) == n && !memcmp(name, w, n); }
enum { UNTIL_END, UNTIL_BRACE, UNTIL_BRACKET, UNTIL_RIGHT, UNTIL_ENV };
static void tx_seq(Tx *t, Buf *o, int until, int inenv);
/* one argument: a {group} or a single token, translated into o */
static void tx_token(Tx *t, Buf *o, int inenv);
static void tx_arg(Tx *t, Buf *o)
{
  skip_ws(t);
  if (tx_peek(t) == '{') { t->i++; tx_seq(t, o, UNTIL_BRACE, 0); }
  else tx_token(t, o, 0);
}
/* an argument as an operand: in parentheses unless it is one character */
static void tx_operand(Tx *t, Buf *o)
{
  Buf a;
  int used;
  buf_init(&a);
  tx_arg(t, &a);
  if (a.data && a.len && utf8_get(a.data, a.len, &used) && (size_t)used == a.len && a.data[0] != ' ') buf_add(o, a.data, a.len);
  else { buf_addc(o, '('); if (a.data) buf_add(o, a.data, a.len); buf_addc(o, ')'); }
  buf_free(&a);
}
/* a delimiter after \left, \right, \big...: its character (0: '.', none) */
static u32 tx_delim(Tx *t)
{
  int c;
  skip_ws(t);
  c = tx_peek(t);
  if (c == '\\') {
    const char *nm;
    size_t n;
    t->i++;
    n = cmd_name(t, &nm);
    if (n == 1 && (*nm == '{' || *nm == '}')) return (u32)*nm;
    if (n == 1 && *nm == '|') return 0x2016;
    return tex_cp(nm, n);
  }
  if (c < 0) return 0;
  {
    int used;
    u32 cp = utf8_get(t->s + t->i, t->n - t->i, &used);
    t->i += (size_t)used;
    return cp == '.' ? 0 : cp;
  }
}
static void tx_env(Tx *t, Buf *o, const char *env, size_t en)
{
  const char *pre = "", *open = "\xe2\x96\xa0(", *post = "";       /* ■( */
  if (is_cmd(env, en, "pmatrix")) { pre = "("; post = ")"; }
  else if (is_cmd(env, en, "bmatrix")) { pre = "["; post = "]"; }
  else if (is_cmd(env, en, "Bmatrix")) { pre = "{"; post = "}"; }
  else if (is_cmd(env, en, "vmatrix")) { pre = "|"; post = "|"; }
  else if (is_cmd(env, en, "Vmatrix")) { pre = "\xe2\x80\x96"; post = "\xe2\x80\x96"; }
  else if (is_cmd(env, en, "cases")) { pre = "{"; open = "\xe2\x96\x88("; post = "\xe2\x94\xa4"; }   /* {█( … )┤ */
  else if (!(is_cmd(env, en, "matrix") || is_cmd(env, en, "smallmatrix") || is_cmd(env, en, "array"))) open = "\xe2\x96\x88(";   /* aligned, gathered...: █( */
  if (is_cmd(env, en, "array")) { skip_ws(t); if (tx_peek(t) == '{') { while (t->i < t->n && t->s[t->i] != '}') t->i++; if (t->i < t->n) t->i++; } }
  buf_addc(o, ' ');
  buf_adds(o, pre); buf_adds(o, open);
  tx_seq(t, o, UNTIL_ENV, 1);
  buf_addc(o, ')'); buf_adds(o, post);
  buf_addc(o, ' ');
}
static void tx_command(Tx *t, Buf *o, int inenv)
{
  const char *nm;
  size_t n = cmd_name(t, &nm);
  u32 cp;
  int k;
  if (!n) return;
  if (n == 1 && !((*nm | 32) >= 'a' && (*nm | 32) <= 'z')) {
    switch (*nm) {
    case '\\': buf_addc(o, inenv ? '@' : ' '); return;
    case '{': buf_addc(o, '{'); return;
    case '}': buf_addc(o, '}'); return;
    case '|': put_u(o, 0x2016); return;
    case ',': case ':': case ';': put_u(o, 0x2009); return;
    case '!': return;
    case ' ': buf_addc(o, ' '); return;
    case '#': case '%': case '$': case '&': case '_': buf_addc(o, *nm); return;
    }
    buf_addc(o, *nm);
    return;
  }
  if (is_cmd(nm, n, "frac") || is_cmd(nm, n, "dfrac") || is_cmd(nm, n, "tfrac") || is_cmd(nm, n, "cfrac")) {
    buf_adds(o, " ("); tx_arg(t, o); buf_adds(o, ")/("); tx_arg(t, o); buf_adds(o, ") ");
    return;
  }
  if (is_cmd(nm, n, "binom") || is_cmd(nm, n, "dbinom") || is_cmd(nm, n, "tbinom")) {
    buf_adds(o, " ("); tx_arg(t, o); buf_adds(o, "\xc2\xa6"); tx_arg(t, o); buf_adds(o, ") ");
    return;
  }
  if (is_cmd(nm, n, "sqrt")) {
    put_u(o, 0x221A); buf_addc(o, '(');
    skip_ws(t);
    if (tx_peek(t) == '[') { t->i++; tx_seq(t, o, UNTIL_BRACKET, 0); buf_addc(o, '&'); }
    tx_arg(t, o);
    buf_addc(o, ')');
    return;
  }
  if (is_cmd(nm, n, "left")) {
    u32 b = tx_delim(t), e;
    Buf in;
    buf_init(&in);
    tx_seq(t, &in, UNTIL_RIGHT, inenv);
    e = tx_delim(t);
    if (!b || !e || (b != e && (b == '|' || b == 0x2016))) { put_u(o, 0x251C); if (b) put_u(o, b); }
    else put_u(o, b);
    if (in.data) buf_add(o, in.data, in.len);
    if (!b || !e || (b != e && (b == '|' || b == 0x2016))) { if (e) put_u(o, e); put_u(o, 0x2524); }
    else put_u(o, e);
    buf_free(&in);
    return;
  }
  if (is_cmd(nm, n, "middle")) { (void)tx_delim(t); put_u(o, 0x2502); return; }
  if (is_cmd(nm, n, "big") || is_cmd(nm, n, "Big") || is_cmd(nm, n, "bigg") || is_cmd(nm, n, "Bigg") || is_cmd(nm, n, "bigl") || is_cmd(nm, n, "bigr") ||
      is_cmd(nm, n, "Bigl") || is_cmd(nm, n, "Bigr")) { u32 d = tx_delim(t); if (d) put_u(o, d); return; }
  if (is_cmd(nm, n, "limits") || is_cmd(nm, n, "nolimits") || is_cmd(nm, n, "displaystyle") || is_cmd(nm, n, "textstyle") || is_cmd(nm, n, "nonumber") ||
      is_cmd(nm, n, "notag")) return;
  if (is_cmd(nm, n, "text") || is_cmd(nm, n, "textrm") || is_cmd(nm, n, "mathrm") || is_cmd(nm, n, "mbox") || is_cmd(nm, n, "textit") ||
      is_cmd(nm, n, "textbf") || is_cmd(nm, n, "operatorname")) {
    /* its text as it is: normal text */
    size_t a, depth = 0;
    skip_ws(t);
    if (tx_peek(t) != '{') { tx_token(t, o, inenv); return; }
    a = ++t->i;
    while (t->i < t->n && !(t->s[t->i] == '}' && !depth)) { if (t->s[t->i] == '{') depth++; else if (t->s[t->i] == '}') depth--; t->i++; }
    buf_addc(o, '"'); buf_add(o, t->s + a, t->i - a); buf_addc(o, '"');
    if (t->i < t->n) t->i++;
    return;
  }
  if (is_cmd(nm, n, "mathbb") || is_cmd(nm, n, "mathcal") || is_cmd(nm, n, "mathscr") || is_cmd(nm, n, "mathfrak")) {
    /* the letters' own characters where Unicode has them in its first plane */
    static const char bbL[] = "CHNPQRZ", calL[] = "BEFHILMR", frL[] = "CHIRZ";
    static const u32 bbC[] = { 0x2102, 0x210D, 0x2115, 0x2119, 0x211A, 0x211D, 0x2124 }, calC[] = { 0x212C, 0x2130, 0x2131, 0x210B, 0x2110, 0x2112, 0x2133, 0x211B },
                     frC[] = { 0x212D, 0x210C, 0x2111, 0x211C, 0x2128 };
    Buf a;
    const char *L = nm[4] == 'b' ? bbL : nm[4] == 'f' ? frL : calL;
    const u32 *C = nm[4] == 'b' ? bbC : nm[4] == 'f' ? frC : calC;
    size_t i;
    buf_init(&a);
    tx_arg(t, &a);
    for (i = 0; a.data && i < a.len; i++) {
      const char *q = strchr(L, a.data[i]);
      if (q && a.data[i]) put_u(o, C[q - L]); else buf_addc(o, a.data[i]);
    }
    buf_free(&a);
    return;
  }
  if (is_cmd(nm, n, "mathbf") || is_cmd(nm, n, "mathit") || is_cmd(nm, n, "mathsf") || is_cmd(nm, n, "mathtt") || is_cmd(nm, n, "boldsymbol") ||
      is_cmd(nm, n, "bm") || is_cmd(nm, n, "mathnormal")) { tx_arg(t, o); return; }
  for (k = 0; tacc[k].name; k++) if (is_cmd(nm, n, tacc[k].name)) { tx_operand(t, o); put_u(o, tacc[k].cp); return; }
  if (is_cmd(nm, n, "overline")) { put_u(o, 0xAF); buf_addc(o, '('); tx_arg(t, o); buf_addc(o, ')'); return; }
  if (is_cmd(nm, n, "underline")) { put_u(o, 0x2581); buf_addc(o, '('); tx_arg(t, o); buf_addc(o, ')'); return; }
  if (is_cmd(nm, n, "overbrace")) { put_u(o, 0x23DE); buf_addc(o, '('); tx_arg(t, o); buf_addc(o, ')'); return; }
  if (is_cmd(nm, n, "underbrace")) { put_u(o, 0x23DF); buf_addc(o, '('); tx_arg(t, o); buf_addc(o, ')'); return; }
  if (is_cmd(nm, n, "boxed") || is_cmd(nm, n, "fbox")) { put_u(o, 0x25AD); buf_addc(o, '('); tx_arg(t, o); buf_addc(o, ')'); return; }
  if (is_cmd(nm, n, "phantom")) { put_u(o, 0x27E1); buf_addc(o, '('); tx_arg(t, o); buf_addc(o, ')'); return; }
  if (is_cmd(nm, n, "overset") || is_cmd(nm, n, "stackrel") || is_cmd(nm, n, "underset")) {
    Buf a;
    int under = nm[0] == 'u';
    buf_init(&a);
    tx_operand(t, &a);
    tx_operand(t, o);
    put_u(o, under ? 0x252C : 0x2534);
    if (a.data) buf_add(o, a.data, a.len);
    buf_free(&a);
    return;
  }
  if (is_cmd(nm, n, "begin")) {
    Buf env;
    buf_init(&env);
    skip_ws(t);
    if (tx_peek(t) == '{') { size_t a = ++t->i; while (t->i < t->n && t->s[t->i] != '}') t->i++; buf_add(&env, t->s + a, t->i - a); if (t->i < t->n) t->i++; }
    if (env.data) { size_t el = env.len; if (el && env.data[el - 1] == '*') el--; tx_env(t, o, env.data, el); }
    buf_free(&env);
    return;
  }
  for (k = 0; tex_funcs[k]; k++) if (is_cmd(nm, n, tex_funcs[k])) {
    buf_add(o, nm, n);
    if (tx_peek(t) != '_' && tx_peek(t) != '^') buf_addc(o, ' ');
    return;
  }
  if ((cp = tex_cp(nm, n))) {
    put_u(o, cp);
    if ((cp >= 0x220F && cp <= 0x2211) || (cp >= 0x222B && cp <= 0x2233) || (cp >= 0x22C0 && cp <= 0x22C3) || (cp >= 0x2A00 && cp <= 0x2A0C)) {
      /* a large operator: its limits come next; its operand after them */
    }
    return;
  }
  if ((cp = math_keyword(nm, n))) { put_u(o, cp); return; }
  buf_addc(o, '\\'); buf_add(o, nm, n);
}
static void tx_token(Tx *t, Buf *o, int inenv)
{
  int c = tx_peek(t), used;
  u32 cp;
  if (c < 0) return;
  if (c == '\\') { t->i++; tx_command(t, o, inenv); return; }
  if (c == '{') { t->i++; tx_seq(t, o, UNTIL_BRACE, inenv); return; }
  cp = utf8_get(t->s + t->i, t->n - t->i, &used);
  t->i += (size_t)used;
  if (cp == '~') cp = ' ';
  put_u(o, cp);
}
static void tx_seq(Tx *t, Buf *o, int until, int inenv)
{
  if (++t->depth > 60) { t->i = t->n; t->depth--; return; }
  while (t->i < t->n) {
    int c = tx_peek(t);
    if (c == '}' ) { t->i++; if (until == UNTIL_BRACE) break; continue; }
    if (c == ']' && until == UNTIL_BRACKET) { t->i++; break; }
    if (c == '\\' && (until == UNTIL_RIGHT || until == UNTIL_ENV)) {
      size_t save = t->i;
      const char *nm;
      size_t n;
      t->i++;
      n = cmd_name(t, &nm);
      if (until == UNTIL_RIGHT && is_cmd(nm, n, "right")) break;
      if (until == UNTIL_ENV && is_cmd(nm, n, "end")) {
        skip_ws(t);
        if (tx_peek(t) == '{') { while (t->i < t->n && t->s[t->i] != '}') t->i++; if (t->i < t->n) t->i++; }
        break;
      }
      t->i = save;
    }
    if (c == '{') {
      /* a group: invisible brackets keep it together */
      Buf g;
      t->i++;
      buf_init(&g);
      tx_seq(t, &g, UNTIL_BRACE, inenv);
      put_u(o, 0x3016); if (g.data) buf_add(o, g.data, g.len); put_u(o, 0x3017);
      buf_free(&g);
      continue;
    }
    if (c == '^' || c == '_') {
      t->i++;
      buf_addc(o, c);
      skip_ws(t);
      if (tx_peek(t) == '{') {
        Buf g;
        t->i++;
        buf_init(&g);
        tx_seq(t, &g, UNTIL_BRACE, inenv);
        buf_addc(o, '('); if (g.data) buf_add(o, g.data, g.len); buf_addc(o, ')');
        buf_free(&g);
      } else tx_token(t, o, inenv);
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { skip_ws(t); if (o->len && o->data[o->len - 1] != ' ') buf_addc(o, ' '); continue; }
    if (c == '&') { t->i++; buf_addc(o, '&'); continue; }
    if (c == '%') { while (t->i < t->n && t->s[t->i] != '\n') t->i++; continue; }
    if (c == '$') { t->i++; continue; }
    tx_token(t, o, inenv);
  }
  t->depth--;
}
void latex_to_linear(const char *s, Buf *o)
{
  Tx t;
  memset(&t, 0, sizeof t);
  t.s = s; t.n = strlen(s);
  tx_seq(&t, o, UNTIL_END, 0);
}
MathEq *math_from_latex(const char *s, int display)
{
  Buf b;
  MathEq *e;
  buf_init(&b);
  latex_to_linear(s, &b);
  e = math_from_linear(b.data ? b.data : "", display);
  buf_free(&b);
  return e;
}

/* ------------------------------------------------------------------ */
/* LaTeX out */
static void lt_row(Buf *o, const MNode *r, int depth);
static void lt_text(Buf *o, const char *s)
{
  size_t i = 0, n = s ? strlen(s) : 0;
  while (i < n) {
    int used;
    u32 c = utf8_get(s + i, n - i, &used);
    const char *nm;
    if (c == 0x2212) buf_addc(o, '-');
    else if (c == 0x2217) buf_addc(o, '*');
    else if (c == 0x2061) ;
    else if (c == 0x2032) buf_addc(o, '\'');
    else if (c == '{' || c == '}') { buf_addc(o, '\\'); buf_addc(o, (int)c); }
    else if (c == 0x2009) buf_adds(o, "\\,");
    else if (c >= 0x80 && (nm = tex_name(c))) {
      buf_addc(o, '\\'); buf_adds(o, nm);
      if (i + (size_t)used < n && ((s[i + used] | 32) >= 'a' && (s[i + used] | 32) <= 'z')) buf_addc(o, ' ');
    } else buf_add(o, s + i, (size_t)used);
    i += (size_t)used;
  }
}
static void lt_group(Buf *o, const MNode *r, int depth) { buf_addc(o, '{'); lt_row(o, r, depth); buf_addc(o, '}'); }
static void lt_delim_char(Buf *o, u32 c, int left)
{
  const char *nm;
  if (!c) { buf_addc(o, '.'); return; }
  if (c == '{' || c == '}') { buf_addc(o, '\\'); buf_addc(o, (int)c); return; }
  if (c == 0x2016) { buf_adds(o, "\\|"); return; }
  if (c < 0x80) { buf_addc(o, (int)c); return; }
  if ((nm = tex_name(c))) { buf_addc(o, '\\'); buf_adds(o, nm); return; }
  (void)left;
  put_u(o, c);
}
static int func_name(const MNode *r, char *out, size_t cap)
{
  int k;
  if (!r || r->nkid != 1 || r->kid[0]->kind != MK_RUN || !r->kid[0]->text) return 0;
  for (k = 0; tex_funcs[k]; k++) if (!strcmp(r->kid[0]->text, tex_funcs[k])) { snprintf(out, cap, "\\%s", tex_funcs[k]); return 1; }
  snprintf(out, cap, "\\operatorname{%s}", r->kid[0]->text);
  return 1;
}
static void lt_node(Buf *o, const MNode *n, int depth)
{
  u16 i;
  if (depth > 60) return;
  switch (n->kind) {
  case MK_ROW: case MK_RAW: lt_row(o, n, depth); break;
  case MK_RUN:
    if (n->pv[P_R_NOR] == MB_ON) { buf_adds(o, "\\text{"); buf_adds(o, n->text ? n->text : ""); buf_addc(o, '}'); }
    else lt_text(o, n->text);
    break;
  case MK_FRAC: {
    int t = n->pv[P_F_TYPE] ? n->pv[P_F_TYPE] - 1 : MF_BAR;
    if (t == MF_BAR) { buf_adds(o, "\\frac"); lt_group(o, n->kid[0], depth + 1); lt_group(o, n->kid[1], depth + 1); }
    else if (t == MF_NOBAR) { buf_addc(o, '{'); lt_row(o, n->kid[0], depth + 1); buf_adds(o, " \\atop "); lt_row(o, n->kid[1], depth + 1); buf_addc(o, '}'); }
    else { lt_group(o, n->kid[0], depth + 1); buf_addc(o, '/'); lt_group(o, n->kid[1], depth + 1); }
    break; }
  case MK_SSUB: case MK_SSUP: case MK_SSUBSUP:
    lt_group(o, n->kid[0], depth + 1);
    if (n->kind != MK_SSUP) { buf_addc(o, '_'); lt_group(o, n->kid[1], depth + 1); }
    if (n->kind != MK_SSUB) { buf_addc(o, '^'); lt_group(o, n->kid[n->kind == MK_SSUP ? 1 : 2], depth + 1); }
    break;
  case MK_SPRE:
    buf_adds(o, "{}_"); lt_group(o, n->kid[0], depth + 1); buf_addc(o, '^'); lt_group(o, n->kid[1], depth + 1); lt_group(o, n->kid[2], depth + 1);
    break;
  case MK_RAD:
    buf_adds(o, "\\sqrt");
    if (n->pv[P_RAD_DEGHIDE] != MB_ON && n->kid[0]->nkid) { buf_addc(o, '['); lt_row(o, n->kid[0], depth + 1); buf_addc(o, ']'); }
    lt_group(o, n->kid[1], depth + 1);
    break;
  case MK_NARY: {
    u32 c = n->ch[0] == MCH_UNSET ? 0x222B : n->ch[0];
    const char *nm = tex_name(c);
    if (nm) { buf_addc(o, '\\'); buf_adds(o, nm); } else put_u(o, c);
    if (n->pv[P_N_LIMLOC] == MLIM_UNDOVR + 1 && c >= 0x222B && c <= 0x2233) buf_adds(o, "\\limits");
    if (n->pv[P_N_SUBHIDE] != MB_ON) { buf_addc(o, '_'); lt_group(o, n->kid[0], depth + 1); }
    if (n->pv[P_N_SUPHIDE] != MB_ON) { buf_addc(o, '^'); lt_group(o, n->kid[1], depth + 1); }
    lt_group(o, n->kid[2], depth + 1);
    buf_addc(o, ' ');                    /* (the operand ends here when read back) */
    break; }
  case MK_DELIM: {
    u32 b = n->ch[0] == MCH_UNSET ? '(' : n->ch[0], e = n->ch[2] == MCH_UNSET ? ')' : n->ch[2];
    buf_adds(o, "\\left"); lt_delim_char(o, b, 1);
    for (i = 0; i < n->nkid; i++) {
      if (i) { u32 s = n->ch[1] == MCH_UNSET || !n->ch[1] ? '|' : n->ch[1]; buf_adds(o, "\\middle"); lt_delim_char(o, s == 0x2502 ? '|' : s, 0); }
      lt_row(o, n->kid[i], depth + 1);
    }
    buf_adds(o, "\\right"); lt_delim_char(o, e, 0);
    break; }
  case MK_FUNC: {
    char fn[96];
    const MNode *nm = n->kid[0];
    if (func_name(nm, fn, sizeof fn)) buf_adds(o, fn);
    else if (nm->nkid == 1 && (nm->kid[0]->kind == MK_LIMLOW || nm->kid[0]->kind == MK_SSUP || nm->kid[0]->kind == MK_SSUB) && func_name(nm->kid[0]->kid[0], fn, sizeof fn)) {
      const MNode *s = nm->kid[0];
      buf_adds(o, fn);
      buf_addc(o, s->kind == MK_SSUP ? '^' : '_');
      lt_group(o, s->kid[1], depth + 1);
    } else lt_group(o, nm, depth + 1);
    lt_group(o, n->kid[1], depth + 1);
    break; }
  case MK_ACC: {
    u32 c = n->ch[0] == MCH_UNSET ? 0x302 : n->ch[0];
    int k;
    for (k = 0; tacc[k].name && tacc[k].cp != c; k++) ;
    if (tacc[k].name) { buf_addc(o, '\\'); buf_adds(o, tacc[k].name); }
    else { buf_adds(o, "\\overset{"); put_u(o, c); buf_addc(o, '}'); }
    lt_group(o, n->kid[0], depth + 1);
    break; }
  case MK_BAR: buf_adds(o, n->pv[P_BAR_POS] == MPOS_TOP + 1 ? "\\overline" : "\\underline"); lt_group(o, n->kid[0], depth + 1); break;
  case MK_GROUPCHR: {
    u32 c = n->ch[0] == MCH_UNSET ? 0x23DF : n->ch[0];
    if (c == 0x23DE) buf_adds(o, "\\overbrace");
    else if (c == 0x23DF) buf_adds(o, "\\underbrace");
    else { buf_adds(o, n->pv[P_G_POS] == MPOS_TOP + 1 ? "\\overset{" : "\\underset{"); put_u(o, c); buf_addc(o, '}'); }
    lt_group(o, n->kid[0], depth + 1);
    break; }
  case MK_LIMLOW: case MK_LIMUPP:
    buf_adds(o, n->kind == MK_LIMLOW ? "\\underset" : "\\overset");
    lt_group(o, n->kid[1], depth + 1); lt_group(o, n->kid[0], depth + 1);
    break;
  case MK_BOX: lt_group(o, n->kid[0], depth + 1); break;
  case MK_BORDERBOX: buf_adds(o, "\\boxed"); lt_group(o, n->kid[0], depth + 1); break;
  case MK_PHANT: buf_adds(o, "\\phantom"); lt_group(o, n->kid[0], depth + 1); break;
  case MK_MATRIX:
    buf_adds(o, "\\begin{matrix}");
    for (i = 0; i < n->nkid; i++) {
      if (i) buf_adds(o, n->cols && i % n->cols ? "&" : "\\\\");
      lt_row(o, n->kid[i], depth + 1);
    }
    buf_adds(o, "\\end{matrix}");
    break;
  case MK_EQARR:
    if (n->nkid == 1) {
      const MNode *r = n->kid[0];
      for (i = 0; i < r->nkid; i++) if (r->kid[i]->kind == MK_RUN && r->kid[i]->pv[P_R_NOR] != MB_ON && r->kid[i]->text && strchr(r->kid[i]->text, '#')) break;
      if (i < r->nkid) { lt_row(o, r, depth + 1); break; }      /* a numbered equation: E=mc^2#(1) */
    }
    buf_adds(o, "\\begin{aligned}");
    for (i = 0; i < n->nkid; i++) { if (i) buf_adds(o, "\\\\"); lt_row(o, n->kid[i], depth + 1); }
    buf_adds(o, "\\end{aligned}");
    break;
  }
}
static void lt_row(Buf *o, const MNode *r, int depth)
{
  u16 i;
  for (i = 0; i < r->nkid; i++) lt_node(o, r->kid[i], depth);
}
void math_to_latex(const MathEq *e, Buf *o)
{
  u16 i;
  for (i = 0; i < e->nline; i++) { if (i) buf_adds(o, "\\\\"); lt_row(o, e->line[i], 0); }
}
