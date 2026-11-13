#ifndef _ASCC_DIRENT_H
#define _ASCC_DIRENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
struct dirent {
  __uint64_t d_ino;
  __uint64_t d_seekoff;
  __uint16_t d_reclen;
  __uint16_t d_namlen;
  __uint8_t d_type;
  char d_name[1024];
};
#define d_fileno d_ino
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
typedef struct _ascc_DIR DIR;
int closedir(DIR *);
DIR *opendir(const char *);
DIR *fdopendir(int);
struct dirent *readdir(DIR *);
int readdir_r(DIR *, struct dirent *, struct dirent **);
void rewinddir(DIR *);
void seekdir(DIR *, long);
long telldir(DIR *);
int dirfd(DIR *);
int scandir(const char *, struct dirent ***, int (*)(const struct dirent *), int (*)(const struct dirent **, const struct dirent **));
int alphasort(const struct dirent **, const struct dirent **);
#ifdef __cplusplus
}
#endif
#endif
