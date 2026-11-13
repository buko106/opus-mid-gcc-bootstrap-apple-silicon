#ifndef _ASCC_TERMIOS_H
#define _ASCC_TERMIOS_H
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned long tcflag_t;
typedef unsigned char cc_t;
typedef unsigned long speed_t;
struct termios { tcflag_t c_iflag; tcflag_t c_oflag; tcflag_t c_cflag; tcflag_t c_lflag; cc_t c_cc[20]; speed_t c_ispeed; speed_t c_ospeed; };
int tcgetattr(int, struct termios *);
int tcsetattr(int, int, const struct termios *);
#define TCSANOW 0
#ifdef __cplusplus
}
#endif
#endif
