# ascc: a self-hosting C compiler, assembler and linker for arm64 macOS,
# and the bootstrap chain clang -> ascc -> GCC 4.7 -> GCC 10.5.
#
#   make stage1   ascc built by clang (the only use of clang)
#   make stage2   ascc built by stage1 ascc
#   make stage3   ascc built by stage2 ascc; must be identical to stage2
#   make test     unit tests, coverage, self-host check, asm checks

ROOT     := $(abspath .)
BUILD    := $(ROOT)/build
SDKROOT  ?= /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
SRCS     := src/main.c src/util.c src/as.c src/ld.c src/ar.c src/tokenize.c \
            src/preprocess.c src/type.c src/parse.c src/codegen.c src/driver.c
HDRS     := src/util.h src/macho.h src/cc.h
DEFS     := -DASCC_INCLUDE_DIR='"$(ROOT)/include"' -DASCC_SYSROOT='"$(SDKROOT)"'
SEED_CC  ?= clang
SEED_CFLAGS ?= -std=c99 -O1 -g -Wall -Wno-unused-function -nostdinc -I$(ROOT)/include

.PHONY: all stage1 stage2 stage3 selfhost test clean

all: stage3

$(BUILD)/stage1/ascc: $(SRCS) $(HDRS) $(wildcard include/*.h include/*/*.h)
	@mkdir -p $(dir $@)
	$(SEED_CC) $(SEED_CFLAGS) $(DEFS) -o $@ $(SRCS)
	cd $(dir $@) && ln -sf ascc as && ln -sf ascc ld && ln -sf ascc ar && ln -sf ascc ranlib && ln -sf ascc cc

$(BUILD)/stage2/ascc: $(BUILD)/stage1/ascc
	@mkdir -p $(dir $@)
	$(BUILD)/stage1/ascc $(DEFS) -o $@ $(SRCS)
	cd $(dir $@) && ln -sf ascc as && ln -sf ascc ld && ln -sf ascc ar && ln -sf ascc ranlib && ln -sf ascc cc

$(BUILD)/stage3/ascc: $(BUILD)/stage2/ascc
	@mkdir -p $(dir $@)
	$(BUILD)/stage2/ascc $(DEFS) -o $@ $(SRCS)
	cd $(dir $@) && ln -sf ascc as && ln -sf ascc ld && ln -sf ascc ar && ln -sf ascc ranlib && ln -sf ascc cc

stage1: $(BUILD)/stage1/ascc
stage2: $(BUILD)/stage2/ascc
stage3: $(BUILD)/stage3/ascc

selfhost: $(BUILD)/stage3/ascc
	cmp $(BUILD)/stage2/ascc $(BUILD)/stage3/ascc
	@echo "selfhost: stage2 and stage3 are identical"

clean:
	rm -rf $(BUILD)
