#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int test_failures;
#define ASSERT(want, got) assert_eq(__LINE__, #got, (long long)(want), (long long)(got))
static void assert_eq(int line, const char *expr, long long want, long long got) {
  if (want != got) {
    printf("FAIL line %d: %s => %lld, want %lld\n", line, expr, got, want);
    test_failures++;
  }
}
#define DONE() do { if (test_failures) { printf("%d failures\n", test_failures); return 1; } printf("OK\n"); return 0; } while (0)
