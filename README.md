# ascc: a native bootstrap chain on Apple Silicon

`ascc` is a self-hosting C compiler, assembler, linker and archiver for
arm64 macOS, written for this repository. It starts a chain that ends in a
3-stage bootstrap of GCC 10.5:

```
clang (seed) -> ascc stage1 -> stage2 -> stage3
  -> GCC 4.7 (arm64-darwin port, built by ascc), 3-stage bootstrap
    -> GCC 10.5 (built by GCC 4.7), 3-stage bootstrap
```

clang is used for one thing only: compiling ascc stage1. Every later
compile, assemble, link and archive step uses tools from the chain: ascc,
GCC 4.7, GCC 10.5, and ascc's own `as`, `ld` and `ar`.

## Results

Measured on an Apple M1 Pro (10 cores), macOS 26.7.3, from a clean tree,
running the targets one after another (`build/final.log`).

| Target | Time | Result |
|---|---|---|
| `make test` | < 1 min | self-host stage2 == stage3; asm 21/21 identical to Apple as; diag 131, driver 76, tools 228 passed; coverage 95.2% |
| `make gcc47-arm64` | 3 min | GCC 4.7 built once by ascc; hello world and `cxx-check` OK |
| `make gcc47-bootstrap` | 7 min | GCC's `make compare`: stage2 == stage3 ("Comparison successful"); hello world and `cxx-check` OK |
| `make gcc10-bootstrap` | 17 min | GCC's `make compare`: stage2 == stage3 ("Comparison successful"); every `check-gcc10.sh` check passes |

`gccbuild/check-gcc10.sh` checks the installed GCC 10.5:

- hello world prints `hello from GCC 10.5`;
- ascc built by GCC 10.5 emits the same assembly as the native ascc
  (`build/stage3`) for all 11 ascc source files;
- `gccbuild/tests/cxx-check.cc` (containers, streams, virtual functions,
  exceptions) prints `gccbuild/tests/cxx-check.expected`;
- GCC 10.5 output with and without `-g` (the ascc sources, hello world,
  `cxx-check`) assembled by ascc's `as` is byte-identical to Apple's `as`
  output (`tests/asm_compare.sh`, 25 files, 25 identical, no skips).

The compare is GCC's own `make compare`. It is not disabled, and there is
no `bootstrap4`.

## Usage

```bash
make fetch            # sources only, signature-checked or pinned (see below)
make test             # ascc: unit, self-host, asm, diag, driver, tools, coverage
make gcc47-arm64      # GCC 4.7 arm64 port built by ascc (single stage)
make gcc47-bootstrap  # GCC 4.7 3-stage bootstrap started by ascc
make gcc10-bootstrap  # GCC 10.5 3-stage bootstrap started by GCC 4.7, then the checks
```

`make test` covers:

- self-host: stage2 == stage3;
- C runtime tests (`tests/c`);
- assembler comparison against Apple `as` (`tests/asm`);
- diagnostic tests (`tests/diag`);
- driver tests (`tests/driver.sh`);
- linker, archiver and nm tests (`tests/tools.sh`);
- line coverage of the ascc sources, which must be at least 95%
  (currently 95.2%, 10391/10910 lines).

## What is used, and where the boundaries are

- **Seed:** clang, only for `build/stage1/ascc`.
- **Headers:** ascc's own `include/`. GCC is configured with
  `--with-native-system-header-dir`, so no SDK headers are used.
- **SDK:** only the `.tbd` stub files, read at link time.
  - `gccbuild/host-sdk.sh` picks the SDK that matches the running macOS.
    The stubs carry no per-symbol availability, so a newer SDK would let
    configure find functions such as `pipe2` that this libSystem lacks.
  - `SDKROOT` is unset during GCC builds, because GCC 10's driver would
    turn it into `--sysroot`.
- **Runtime:** libSystem, dyld and the kernel.
- **Build tools:** the GCC builds run under `gccbuild/env.sh`. It puts the
  chain's tools first in `PATH` and replaces the macOS toolchain programs
  (clang, Apple as/ld/ar, libtool, dsymutil and so on) with stubs that
  refuse to run. Generic tools are allowed: make, sh, sed, awk, tar, xz,
  gpg, curl, git, patch and install.
- **Yardstick:** Apple `as` and `otool` are used only to check ascc's
  assembler output. Nothing they produce goes into the build.
- **Sources** (`gccbuild/fetch.sh`):
  - GCC 4.7.4, GCC 10.5.0, GMP 6.1.0, MPFR 3.1.4 and MPC 1.0.3 are
    verified against their GNU signatures with an isolated keyring.
  - The GCC 4.8.0 aarch64 backend is pinned to the commit of the
    `releases/gcc-4.8.0` tag.
  - Iain Sandoe's GCC 10 arm64-darwin port is pinned to a commit range.
  - GMP, MPFR and MPC are built with ascc.

## The GCC ports and patches

- `gccbuild/patches/gcc-4.7/0001-aarch64-darwin-port.patch` ports GCC 4.7
  to arm64 macOS on top of GCC 4.8.0's aarch64 backend:
  - Darwin ABI: variadic arguments on the stack, x18 reserved, signed
    char, 64-bit long double;
  - Mach-O addressing (`@PAGE`, `@GOTPAGEOFF`, ...);
  - libgcc uses the libSystem unwinder;
  - no dsymutil.

  `prepare-gcc47.sh` and `regen-gcc47-patch.sh` reproduce the tree.
- `gccbuild/patches/gcc-10/0001-ascc-chain.patch` makes the changes the
  chain needs, each with a comment explaining why:
  - collect2 never runs dsymutil;
  - `libemutls_w.a` is built without shared libgcc;
  - libgcc uses the libSystem unwinder (the keymgr-based one finds no
    FDEs on modern macOS);
  - two fixes to `lse.S`: the label is placed before `.cfi_startproc`,
    which Mach-O assemblers require, and the Mach-O underscore is added
    to `__aarch64_have_lse_atomics`.

## Layout

- `src/`: ascc (`main.c` dispatches on argv[0]):
  - `as.c`: assembler;
  - `ld.c`: linker;
  - `ar.c`: ar and nm;
  - `tokenize.c`, `preprocess.c`, `parse.c`, `type.c`, `codegen.c`,
    `driver.c`: the compiler;
  - `covrt.c`: coverage runtime.
- `include/`: Darwin-ABI C headers used by ascc and by the GCC builds.
- `tests/`: test suites and scripts.
- `gccbuild/`: fetch, configure, build, check scripts and patches.

## Known limitations

- **Assembler leniency:** ascc's `as` accepts a few malformed instruction
  forms that Apple's `as` rejects (for example `nop x0`, `isb #16`, `b #3`,
  and some SIMD arrangement mismatches in rarely used instructions).
  Compilers do not emit them. Every form a compiler emits is checked
  against Apple's output.
- **CFI:** only `.cfi_startproc` / `.cfi_endproc` frames without CFI
  instructions are supported; they become frameless `__compact_unwind`
  entries. GCC's configure therefore sees no CFI support and writes
  `__eh_frame` itself.
- **Linker:** ascc's `ld` writes PIE executables only (no dylibs). It
  resolves every undefined symbol in the objects it loads, not only the
  referenced ones as ld64 does.
- **No C++ in ascc:** ascc compiles C only. With C++ input it only
  preprocesses, which is all GCC's configure needs. GCC 4.7's later stages
  are built as C (`--disable-build-poststage1-with-cxx`).
