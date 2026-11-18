/* mac/include/math.h - the little floating point Quill's core uses (charts), in software
   (mac/libc.c: no FPU needed, so it runs on 68LC040 and 68030 Macs without one). */
#ifndef QM_MATH_H
#define QM_MATH_H
#define M_PI 3.14159265358979323846
#define HUGE_VAL (__builtin_huge_val())
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double sqrt(double x);
double floor(double x);
double ceil(double x);
double fabs(double x);
double fmod(double x, double y);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double round(double x);
long   lround(double x);
#endif
