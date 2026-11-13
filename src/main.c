/* ascc: multi-call entry point.  The tool is selected by the name it was
   invoked under: as, ld, ar, ranlib; anything else is the C compiler. */
#include "util.h"
#include <string.h>

int main(int argc, char **argv) {
  const char *name = base_name(argv[0]);
  /* allow "ascc as ..." as well as a link named "as" */
  if (argc > 1 && (strcmp(name, "ascc") == 0 || ends_with(name, "-ascc"))) {
    if (strcmp(argv[1], "as") == 0)
      return as_main(argc - 1, argv + 1);
    if (strcmp(argv[1], "ld") == 0)
      return ld_main(argc - 1, argv + 1);
    if (strcmp(argv[1], "ar") == 0 || strcmp(argv[1], "ranlib") == 0)
      return ar_main(argc - 1, argv + 1);
    if (strcmp(argv[1], "nm") == 0)
      return nm_main(argc - 1, argv + 1);
  }
  if (strcmp(name, "as") == 0 || ends_with(name, "-as"))
    return as_main(argc, argv);
  if (strcmp(name, "ld") == 0 || ends_with(name, "-ld"))
    return ld_main(argc, argv);
  if (strcmp(name, "ar") == 0 || ends_with(name, "-ar") ||
      strcmp(name, "ranlib") == 0 || ends_with(name, "-ranlib"))
    return ar_main(argc, argv);
  if (strcmp(name, "nm") == 0 || ends_with(name, "-nm"))
    return nm_main(argc, argv);
  return cc_main(argc, argv);
}
