#ifndef _ASCC_COMPLEX_H
#define _ASCC_COMPLEX_H
#ifdef __cplusplus
extern "C" {
#endif
/* Only usable by compilers with _Complex (GCC); ascc has none. */
#define complex _Complex
#define _Complex_I (__extension__ 1.0iF)
#define I _Complex_I
double complex cacos(double complex); float complex cacosf(float complex);
double complex casin(double complex); float complex casinf(float complex);
double complex catan(double complex); float complex catanf(float complex);
double complex ccos(double complex); float complex ccosf(float complex);
double complex csin(double complex); float complex csinf(float complex);
double complex ctan(double complex); float complex ctanf(float complex);
double complex cacosh(double complex); float complex cacoshf(float complex);
double complex casinh(double complex); float complex casinhf(float complex);
double complex catanh(double complex); float complex catanhf(float complex);
double complex ccosh(double complex); float complex ccoshf(float complex);
double complex csinh(double complex); float complex csinhf(float complex);
double complex ctanh(double complex); float complex ctanhf(float complex);
double complex cexp(double complex); float complex cexpf(float complex);
double complex clog(double complex); float complex clogf(float complex);
double cabs(double complex); float cabsf(float complex);
double complex cpow(double complex, double complex); float complex cpowf(float complex, float complex);
double complex csqrt(double complex); float complex csqrtf(float complex);
double carg(double complex); float cargf(float complex);
double cimag(double complex); float cimagf(float complex);
double complex conj(double complex); float complex conjf(float complex);
double complex cproj(double complex); float complex cprojf(float complex);
double creal(double complex); float crealf(float complex);
long double complex cacosl(long double complex);
long double complex casinl(long double complex);
long double complex catanl(long double complex);
long double complex ccosl(long double complex);
long double complex csinl(long double complex);
long double complex ctanl(long double complex);
long double complex cacoshl(long double complex);
long double complex casinhl(long double complex);
long double complex catanhl(long double complex);
long double complex ccoshl(long double complex);
long double complex csinhl(long double complex);
long double complex ctanhl(long double complex);
long double complex cexpl(long double complex);
long double complex clogl(long double complex);
long double cabsl(long double complex);
long double complex cpowl(long double complex, long double complex);
long double complex csqrtl(long double complex);
long double cargl(long double complex);
long double cimagl(long double complex);
long double complex conjl(long double complex);
long double complex cprojl(long double complex);
long double creall(long double complex);
#ifdef __cplusplus
}
#endif
#endif
