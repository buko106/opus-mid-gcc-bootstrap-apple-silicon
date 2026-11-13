#!/bin/sh
# Fetches the source-only inputs of the bootstrap chain into sources/.
# GNU tarballs are verified against their detached signatures with the GNU
# keyring; git-hosted inputs are pinned to commits.
set -e
cd "$(dirname "$0")/.."
mkdir -p sources
cd sources
GNU=https://ftp.gnu.org/gnu
fetch() { [ -s "$2" ] || curl -fL --retry 3 -o "$2.part" "$1" && { [ -s "$2" ] || mv "$2.part" "$2"; }; }
fetch $GNU/gnu-keyring.gpg gnu-keyring.gpg
# isolated GnuPG home so the user's keyring is untouched
GNUPGHOME=$(mktemp -d)
export GNUPGHOME
trap 'rm -rf "$GNUPGHOME"' EXIT
printf 'no-autostart\n' > "$GNUPGHOME/common.conf"
gpg --batch --quiet --import gnu-keyring.gpg 2>/dev/null || true
for f in gcc/gcc-4.7.4/gcc-4.7.4.tar.bz2 gcc/gcc-10.5.0/gcc-10.5.0.tar.xz \
         gmp/gmp-6.1.0.tar.xz mpfr/mpfr-3.1.4.tar.xz mpc/mpc-1.0.3.tar.gz; do
  b=$(basename $f)
  fetch $GNU/$f $b
  fetch $GNU/$f.sig $b.sig
  LC_ALL=C gpg --batch --verify $b.sig $b
done
# GCC 10.5 arm64-darwin port: Iain Sandoe's gcc-10-branch, branch
# gcc-10-5Dr0-pre-0, pinned to commits (base = the GCC 10.5.0 release).
fetch https://github.com/iains/gcc-10-branch/compare/d04fe5541c53cb16d1ca5c80da044b4c7633dbc6...c749ebf15fdf574e1f2eb352887d33854dd161fa.diff gcc-10.5-darwin-arm64.diff
# GCC 4.8.0 aarch64 backend, pinned to the releases/gcc-4.8.0 commit.
# commit of tag releases/gcc-4.8.0 (tag object 15714406)
C=372a443092ee1656c6d866a16e6da6df32b1f71d
mkdir -p gcc-4.8.0-aarch64
for d in gcc/config/aarch64 libgcc/config/aarch64; do
  l=gcc-4.8.0-aarch64/list.$(echo $d | tr / _)
  [ -s $l ] || curl -fsL "https://api.github.com/repos/gcc-mirror/gcc/contents/$d?ref=$C" |
    sed -n 's/.*"path": "\(.*\)",/\1/p' > $l
  while read p; do
    mkdir -p gcc-4.8.0-aarch64/$(dirname $p)
    fetch "https://raw.githubusercontent.com/gcc-mirror/gcc/$C/$p" gcc-4.8.0-aarch64/$p
  done < $l
done
echo "fetch: done"
