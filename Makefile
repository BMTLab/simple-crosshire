# Makefile for crosshair, a click-through crosshair above every window.
#
# crosshair is a freestanding x86-64 Linux program:
# it uses no C library and no X library (src/kernel.h, src/x11.h),
# so the build needs clang and lld (LLVM 24 by default) and the kernel headers.
#
# The release build is profile-guided in two stages (context-sensitive PGO).
# An instrumented build shows the crosshair a few hundred times (train.sh);
# a second one, shaped by the first profile, counts again by calling context;
# the release build uses both profiles.
# The instrumented builds run on the C library,
# because the profiling runtime writes the counts when main() returns.
# The training needs the X display, and the cross flashes in the screen centre.
#
#   make             the profile-guided release build into ./crosshair
#   make plain       the same flags without profiles; no display needed
#   make install     ./crosshair into $(BINDIR) (default ~/.local/bin)
#   make uninstall   remove it from there
#   make clean       remove the builds and the profiles
#
# Measured on 2026-10-17 (README.md has the numbers): clang 24 beat GCC 16;
# -O3 tied -O2; one segment cut the page faults; both PGO stages gained;
# BOLT gained cycles but added a 2 MB-aligned segment and more page faults,
# so it is not used.

LLVM         ?= $(HOME)/.local/opt/llvm-24/bin
CC           := $(LLVM)/clang
PROFDATA     := $(LLVM)/llvm-profdata
READELF      := $(LLVM)/llvm-readelf
PREFIX       ?= $(HOME)/.local
BINDIR       ?= $(PREFIX)/bin
TRAIN_ROUNDS ?= 200

BUILD   := build
SOURCE  := src/crosshair.c
HEADERS := src/kernel.h src/text.h src/x11.h

# -O3 for this machine's CPU.
OPTIMIZE ?= -O3 -march=native -mtune=native

# A freestanding C23 program: no stack protector (it reads the C library's TLS),
# no unwind tables (nothing unwinds), no PIE (nothing to relocate).
CFLAGS := -std=c23 $(OPTIMIZE) \
          -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wno-sign-conversion \
          -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables -fno-pie \
          -ffunction-sections -fdata-sections

# Static, without start files or libraries.
# The program has no writable data, so with --no-rosegment
# its code and constants share one segment, the only one the kernel maps.
LDFLAGS := -nostdlib -static -fuse-ld=lld -Wl,--gc-sections,--no-rosegment,--build-id=none

# The instrumented builds, on the C library for the profiling runtime.
HOSTED := -DCROSSHAIR_HOSTED -fuse-ld=lld -no-pie

.DEFAULT_GOAL := release

release: $(BUILD)/release/crosshair ## The profile-guided build (default)
	cp -f $< crosshair

plain: $(BUILD)/plain/crosshair ## The build without profiles
	cp -f $< crosshair

$(BUILD)/release/crosshair: $(BUILD)/crosshair.profdata $(SOURCE) $(HEADERS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -fprofile-use=$< $(LDFLAGS) -o $@ $(SOURCE)
	@$(MAKE) --no-print-directory check-stack BINARY=$@

$(BUILD)/plain/crosshair: $(SOURCE) $(HEADERS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCE)
	@$(MAKE) --no-print-directory check-stack BINARY=$@

# Stage 1: plain counts.
# The instrumented program keeps the name crosshair,
# since the toggle recognises a running copy by its process name.
$(BUILD)/stage1/crosshair: $(SOURCE) $(HEADERS)
	rm -rf $(@D) && mkdir -p $(@D)
	$(CC) $(CFLAGS) $(HOSTED) -fprofile-generate=$(CURDIR)/$(@D)/counts -o $@ $(SOURCE)

$(BUILD)/stage1.profdata: $(BUILD)/stage1/crosshair train.sh
	rm -rf $(BUILD)/stage1/counts
	./train.sh $< $(TRAIN_ROUNDS)
	$(PROFDATA) merge -o $@ $(BUILD)/stage1/counts/*.profraw

# Stage 2: context-sensitive counts, on the build the first profile shaped.
$(BUILD)/stage2/crosshair: $(BUILD)/stage1.profdata $(SOURCE) $(HEADERS)
	rm -rf $(@D) && mkdir -p $(@D)
	$(CC) $(CFLAGS) $(HOSTED) -fprofile-use=$< \
	  -fcs-profile-generate=$(CURDIR)/$(@D)/counts -o $@ $(SOURCE)

$(BUILD)/crosshair.profdata: $(BUILD)/stage2/crosshair $(BUILD)/stage1.profdata train.sh
	rm -rf $(BUILD)/stage2/counts
	./train.sh $< $(TRAIN_ROUNDS)
	$(PROFDATA) merge -o $@ $(BUILD)/stage1.profdata $(BUILD)/stage2/counts/*.profraw

# Without a read-write PT_GNU_STACK the kernel would give the process an executable stack.
check-stack:
	@$(READELF) -lW $(BINARY) | grep -q 'GNU_STACK.* RW ' \
	  || { echo "$(BINARY): PT_GNU_STACK is missing or not RW"; rm -f $(BINARY); exit 1; }

# Built on demand when install finds nothing to install.
crosshair:
	@$(MAKE) --no-print-directory release

install: crosshair ## Install ./crosshair into $(BINDIR)
	install -Dm755 crosshair $(DESTDIR)$(BINDIR)/crosshair

uninstall: ## Remove the installed binary
	rm -f $(DESTDIR)$(BINDIR)/crosshair

clean: ## Remove the builds and the profiles
	rm -rf $(BUILD) crosshair

.PHONY: release plain check-stack install uninstall clean
