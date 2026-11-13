#!/bin/sh
# Prints the SDK whose .tbd stubs describe the running macOS.  The .tbd files
# carry no per-symbol availability, so linking against a newer SDK than the
# OS (e.g. MacOSX27 on macOS 26) lets configure scripts find functions such
# as pipe2 that the installed libSystem does not export.
d=/Library/Developer/CommandLineTools/SDKs
v=$(sw_vers -productVersion | cut -d. -f1)
if [ -d "$d/MacOSX$v.sdk" ]; then echo "$d/MacOSX$v.sdk"; else echo "$d/MacOSX.sdk"; fi
