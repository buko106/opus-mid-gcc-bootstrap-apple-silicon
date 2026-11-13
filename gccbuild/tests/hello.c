#include <stdio.h>
int main(void) {
  printf("hello from GCC %d.%d\n", __GNUC__, __GNUC_MINOR__);
  return 0;
}
