# Sourced by the gccbuild scripts.  Sets up a PATH in which every
# toolchain name resolves to a tool built by this chain, or to a stub that
# refuses to run, so that no macOS toolchain program can be used by accident.
#   ASCC_BIN: directory with ascc and its as/ld/ar/ranlib/nm links
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOOLS=$ROOT/build/tools
: "${ASCC_BIN:=$ROOT/build/stage3}"
mkdir -p "$TOOLS"
for t in as ld ar ranlib nm; do ln -sf "$ASCC_BIN/ascc" "$TOOLS/$t"; done
ln -sf "$ASCC_BIN/ascc" "$TOOLS/ascc"
for t in clang clang++ cc gcc g++ c++ cpp strip libtool lipo dsymutil otool \
         ld64 ld-classic codesign_allocate install_name_tool segedit size \
         strings objdump llvm-ar llvm-nm llvm-objdump xcrun; do
  cat > "$TOOLS/$t" <<STUB
#!/bin/sh
echo "gccbuild: refusing to run forbidden tool '$t' (\$*)" >&2
exit 127
STUB
  chmod +x "$TOOLS/$t"
done
PATH=$TOOLS:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export PATH
# GCC 10's Darwin driver turns $SDKROOT into --sysroot; keep it unset so the
# headers come from ascc's include/ (--with-native-system-header-dir).
unset SDKROOT
export MACOSX_DEPLOYMENT_TARGET=11.0
unset CPATH LIBRARY_PATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH SDKROOT_ENV
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
