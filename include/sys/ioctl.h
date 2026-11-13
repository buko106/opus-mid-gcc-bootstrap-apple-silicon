#ifndef _ASCC_SYS_IOCTL_H
#define _ASCC_SYS_IOCTL_H
#ifdef __cplusplus
extern "C" {
#endif
struct winsize { unsigned short ws_row; unsigned short ws_col; unsigned short ws_xpixel; unsigned short ws_ypixel; };
#define TIOCGWINSZ 0x40087468UL
int ioctl(int, unsigned long, ...);
#ifdef __cplusplus
}
#endif
#endif
