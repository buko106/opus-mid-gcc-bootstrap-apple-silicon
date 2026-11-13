#ifndef _ASCC_SPAWN_H
#define _ASCC_SPAWN_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <signal.h>
typedef void *posix_spawnattr_t;
typedef void *posix_spawn_file_actions_t;
int posix_spawn(pid_t *__restrict, const char *__restrict, const posix_spawn_file_actions_t *, const posix_spawnattr_t *__restrict, char *const[], char *const[]);
int posix_spawnp(pid_t *__restrict, const char *__restrict, const posix_spawn_file_actions_t *, const posix_spawnattr_t *__restrict, char *const[], char *const[]);
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *, int);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *, int, int);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *__restrict, int, const char *__restrict, int, mode_t);
#ifdef __cplusplus
}
#endif
#endif
