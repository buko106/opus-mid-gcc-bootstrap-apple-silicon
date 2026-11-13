#ifndef _ASCC_SYS_FILE_H
#define _ASCC_SYS_FILE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#define L_SET SEEK_SET
#define L_INCR SEEK_CUR
#define L_XTND SEEK_END
#ifdef __cplusplus
}
#endif
#endif
