/* ascc: runtime for -fascc-coverage.  Linked only into instrumented
   builds.  Each instrumented translation unit registers a record from a
   static constructor; at exit (or before a forked tool child exits) the
   counters are appended to $ASCC_COVDIR/cov-<pid>.txt as "file line count"
   lines.  tests/coverage.sh merges and reports them. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct ascc_cov_rec {
  struct ascc_cov_rec *next;
  long n;
  long *counters;
  int *lines;
  char **files;
};

static struct ascc_cov_rec *head;
extern void (*ascc_cov_hook)(void);

static void ascc_cov_dump(void) {
  const char *dir = getenv("ASCC_COVDIR");
  char path[1024];
  FILE *fp;
  struct ascc_cov_rec *r;
  if (!dir || !*dir)
    return;
  snprintf(path, sizeof(path), "%s/cov-%d.txt", dir, (int)getpid());
  fp = fopen(path, "a");
  if (!fp)
    return;
  for (r = head; r; r = r->next) {
    long i;
    for (i = 0; i < r->n; i++) {
      fprintf(fp, "%s %d %ld\n", r->files[i], r->lines[i], r->counters[i]);
      r->counters[i] = 0;
    }
  }
  fclose(fp);
}

void ascc_cov_register(struct ascc_cov_rec *r) {
  if (!head) {
    atexit(ascc_cov_dump);
    ascc_cov_hook = ascc_cov_dump;
  }
  r->next = head;
  head = r;
}
