#ifndef QUILL_SPLASH_H
#define QUILL_SPLASH_H
/* the window shown while Quill starts (splash.c) */
void splash_open(const char *screen, const char *title);
void splash_step(int pct, const char *what);
void splash_front(void);
void splash_close(void);
#endif
