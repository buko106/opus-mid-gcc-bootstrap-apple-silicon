#ifndef GUARDED_H
#define GUARDED_H
#if defined(GUARD_EXTRA)
#ifdef GUARD_MORE
int guard_never;
#endif
#else
static int guarded_value = 11;
#endif
#endif
