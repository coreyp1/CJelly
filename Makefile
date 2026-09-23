SUITE := ghoti.io
PROJECT := cjelly

BUILD ?= release
# The version of this library. MINOR_VERSION carries the minor and the patch as
# one dotted string; the two are split out below for the places that need three
# separate integers. See CONVENTIONS.md section 4.
MAJOR_VERSION := 0
MINOR_VERSION := 0.0
VERSION_MINOR_ONLY := $(word 1,$(subst ., ,$(MINOR_VERSION)))
VERSION_PATCH_ONLY := $(or $(word 2,$(subst ., ,$(MINOR_VERSION))),0)
# Substituted into the .pc file; an empty Version: field makes every
# pkg-config version constraint fail.
VERSION := $(MAJOR_VERSION).$(MINOR_VERSION)

# Names this build everywhere: the .pc file, the install directory, the soname
# and the symbol token. It defaults to the major version, so an ordinary build
# of 1.x is "-1" and two majors cannot be loaded into one process by mistake.
# Override it for a build that wants its own identity:  make BRANCH=-dev
BRANCH ?= -$(MAJOR_VERSION)

# What the library reports as its version. The branch is appended only when it
# is not the default, so an ordinary build says "1.2.3" and an overridden one
# says "1.2.3-dev". Computed before BUILD=debug rewrites BRANCH below.
ifeq ($(BRANCH),-$(MAJOR_VERSION))
VERSION_STRING := $(VERSION)
else
VERSION_STRING := $(VERSION)$(BRANCH)
endif

# If BUILD is debug, append -debug.
#
# "override" because BRANCH may have come from the command line, and a
# command-line variable otherwise wins over a plain assignment here: without it
# `make BRANCH=-dev BUILD=debug` produced a debug build carrying the release
# token, whose symbols collide with the release build's.
ifeq ($(BUILD),debug)
    override BRANCH := $(BRANCH)-debug
    override VERSION_STRING := $(VERSION_STRING)-debug
endif

BASE_NAME := lib$(SUITE)-$(PROJECT)$(BRANCH).so
# The symbol namespace token, from BRANCH. See CONVENTIONS.md section 4.
LIBVER_SYMBOL := $(shell echo "ghotiio_$(PROJECT)$(BRANCH)" | sed 's/[.-]/_/g')

BASE_NAME_PREFIX := lib$(SUITE)-$(PROJECT)$(BRANCH)
STATIC_TARGET := $(BASE_NAME_PREFIX).a
SO_NAME := $(BASE_NAME).$(MAJOR_VERSION)
ENV_VARS :=

# PKG_CONFIG_PATH names where this project's own .pc file is installed, and the
# platform block below overwrites it to say so. Remember what the environment
# asked for first, so dependency lookup can still honour it further down.
PKG_CONFIG_PATH_ENV := $(PKG_CONFIG_PATH)

# `override` on each of those: BUILD may arrive on the command line, and a
# command-line variable beats a plain makefile assignment, so without it
# `make BUILD=debug` skips the rewrite and builds into ./build/debug --
# outside the platform tree, and a different tree from the one plain `make`
# uses. The platform segment exists to keep linux/mac/win builds apart.

# Detect OS
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	OS_NAME := Linux
	LIB_EXTENSION := so
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-soname,$(SO_NAME)
	TARGET := $(SO_NAME).$(MINOR_VERSION)
	EXE_EXTENSION :=
	# Additional Linux-specific variables
	PKG_CONFIG_PATH := /usr/local/share/pkgconfig
	INCLUDE_INSTALL_PATH := /usr/local/include
	LIB_INSTALL_PATH := /usr/local/lib
	ENV_VARS += VK_LAYER_PATH=/usr/share/vulkan/explicit_layer.d
	override BUILD := linux/$(BUILD)

else ifeq ($(UNAME_S), Darwin)
	OS_NAME := Mac
	LIB_EXTENSION := dylib
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-install_name,$(BASE_NAME_PREFIX).dylib
	TARGET := $(BASE_NAME_PREFIX).dylib
	EXE_EXTENSION :=
	# Additional macOS-specific variables
	override BUILD := mac/$(BUILD)

else ifeq ($(findstring MINGW32_NT,$(UNAME_S)),MINGW32_NT)  # 32-bit Windows
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	# Additional Windows-specific variables
	# This is the path to the pkg-config files on MSYS2
	PKG_CONFIG_PATH := /mingw32/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw32/include
	LIB_INSTALL_PATH := /mingw32/lib
	BIN_INSTALL_PATH := /mingw32/bin
	override BUILD := win32/$(BUILD)

else ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)  # 64-bit Windows
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	# Additional Windows-specific variables
	# This is the path to the pkg-config files on MSYS2
	PKG_CONFIG_PATH := /mingw64/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw64/include
	LIB_INSTALL_PATH := /mingw64/lib
	BIN_INSTALL_PATH := /mingw64/bin
	ENV_VARS += VK_LAYER_PATH=/mingw64/bin/VkLayer_khronos_validation.json
	override BUILD := win64/$(BUILD)

else
    $(error Unsupported OS: $(UNAME_S))

endif

# ---------------------------------------------------------------------------
# Installation prefix
#
# Defaults to the system location chosen above. Override it to install
# somewhere else - the suite's bootstrap installs every library into a local
# prefix so that each build resolves its dependencies through pkg-config,
# exactly as a consumer would, rather than through a second code path that
# only in-tree builds exercise. See CONVENTIONS.md section 1.
#
#     make install PREFIX=/path/to/prefix
# ---------------------------------------------------------------------------
ifdef PREFIX
INCLUDE_INSTALL_PATH := $(PREFIX)/include
LIB_INSTALL_PATH := $(PREFIX)/lib
BIN_INSTALL_PATH := $(PREFIX)/bin
PKG_CONFIG_PATH := $(PREFIX)/share/pkgconfig
ifeq ($(OS_NAME), Windows)
PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
else
PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
endif
# A non-system prefix has no /etc/ld.so.conf.d, and writing to it would need
# root anyway. Everything built here carries an rpath to the prefix instead.
LDCONF_INSTALL_PATH :=
endif

# Dependencies are looked up along the inherited PKG_CONFIG_PATH as well as the
# install location chosen above, so that exporting PKG_CONFIG_PATH works as the
# errors below say it does. The inherited value comes first: it is an explicit
# request for this build, where the install location may be only a default.
PKG_CONFIG_LOOKUP_PATH := $(if $(PKG_CONFIG_PATH_ENV),$(PKG_CONFIG_PATH_ENV):)$(PKG_CONFIG_PATH)


# The optimization level is the one thing that distinguishes the two builds'
# compile flags. `release` is what gets installed and what anything linking
# against this library actually runs, so it is compiled for speed; `debug` is
# compiled for stepping through. -g stays in both, because a release build
# that cannot be read in a debugger is a release build nobody can diagnose,
# and the symbols cost only file size.
#
# Both builds were -O0 until now, which meant `BUILD=debug` renamed the
# artifact and changed nothing about how it was compiled, and the shipped
# library was the debugging build under another name.
#
# The sanitizer build puts its own -O1 after this one (see ASAN_UBSAN_FLAGS),
# the fuzz build its own -O1, and `make coverage` its own -O0, all by
# appending, since the last -O on the command line wins.
ifeq ($(BUILD),debug)
OPT_CFLAGS := -O0
else
OPT_CFLAGS := -O2
endif

# -Wall sets -Wstrict-aliasing to 3, which is silent on an ordinary type pun;
# level 1 is the only one that diagnoses one. Naming it is therefore not
# adding a warning to a build that had none - it is replacing a level that
# reports nothing with the level that reports. Verified with
# `gcc -Q --help=warnings`: bare gives 0, -Wall gives 3, and this gives 1.
#
# What can disarm it is a LATER EXPLICIT LEVEL, and only that. CFLAGS ends
# with $(EXTRA_CFLAGS), so `EXTRA_CFLAGS=-Wstrict-aliasing=3` switches the
# warning off from the command line with every flag still on the line and
# every comment here still true. check-aliasing is what notices.
#
# Its position relative to -Wall does NOT matter, which is worth writing down
# because the opposite is easy to assume and was believed in this suite until
# it was measured. -Wall's 3 is a default that any explicit level beats from
# either side: `-Wall -Wstrict-aliasing=1` and `-Wstrict-aliasing=1 -Wall`
# both resolve to 1 and both diagnose the control. Only explicit-against-
# explicit is positional - `=3` after `=1` gives 3, and `=1` after `=3`
# gives 1.
#
# Only CFLAGS: the library is C, and the C++ here is test code.
ALIASING_CFLAGS := -Wstrict-aliasing=1

# `f()` is not an empty parameter list in C17, it is a declaration with no
# prototype, and C23 redefines it. Seven definitions here were written that
# way and nothing said so: it is in neither -Wall nor -Wextra. clang rejects
# them - which is how they were found, when `make CC=clang` could not get past
# the seventh object - so without this flag the fix stays fixed only for as
# long as nobody writes another one before the next clang attempt.
#
# gcc catches SIX of those seven shapes, not all of them. It suppresses the
# warning when a prototype for the same function is already in scope, so a
# definition like processWindowEvents, declared `(void)` earlier in the file
# and defined `()`, compiles silently here and is an error under clang.
# Measured both ways by putting each shape back one at a time. So this is a
# gate against writing a new one, not a claim that gcc sees what clang sees.
#
# Only CFLAGS, and for a second reason: g++ rejects it as valid for C but not
# for C++, so putting it anywhere CXXFLAGS can reach would warn on every C++
# compile.
PROTOTYPE_CFLAGS := -Wstrict-prototypes

CXX := g++
CXXFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wfatal-errors -std=c++20 -O1 -g $(EXTRA_CXXFLAGS)
CC := cc
CFLAGS := -pedantic-errors -Wall -Wextra $(ALIASING_CFLAGS) $(PROTOTYPE_CFLAGS) -Werror -Wfatal-errors -std=c17 $(OPT_CFLAGS) -g `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags vulkan` $(EXTRA_CFLAGS)
# Library-specific compile flags (export symbols on Windows, PIC on Linux)
# The shipped library exports its public API and nothing else. Tests reach the
# internals by linking the static archive, which a static link can do even for
# hidden symbols.
LIB_CFLAGS := $(CFLAGS) -fvisibility=hidden -DCJELLY_BUILD $(EXTRA_CFLAGS)
# -DGHOTIIO_CUTIL_ENABLE_MEMORY_DEBUG
LDFLAGS := -L /usr/lib -lstdc++ -lm `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs --cflags vulkan` $(EXTRA_LDFLAGS)
ifdef PREFIX
# So that a library, a test or an example finds its Ghoti.io dependencies in the
# prefix at run time without LD_LIBRARY_PATH.
LDFLAGS += -Wl,-rpath,$(LIB_INSTALL_PATH)/$(SUITE)
endif

BUILD_DIR := ./build/$(BUILD)
OBJ_DIR := $(BUILD_DIR)/objects
FLAGS_STAMP := $(OBJ_DIR)/.flags
GEN_DIR := $(BUILD_DIR)/generated
APP_DIR := $(BUILD_DIR)/apps


# Add OS-specific flags
ifeq ($(UNAME_S), Linux)
	CFLAGS += `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags x11`
	LDFLAGS += `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs x11`
	# XInput2 (libXi) is optional - if not available, we fall back to traditional events
	# For now, we'll skip linking libXi and make XInput2 optional at runtime
	# This avoids linker issues - XInput2 functions will be called only if available
	# TODO: Add proper libXi linking when needed for full XInput2 support
	LIB_CFLAGS += -fPIC

else ifeq ($(UNAME_S), Darwin)

else ifeq ($(findstring MINGW32_NT,$(UNAME_S)),MINGW32_NT)  # 32-bit Windows
	LDFLAGS += -lgdi32

else ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)  # 64-bit Windows
	LDFLAGS += -lgdi32

else
	$(error Unsupported OS: $(UNAME_S))

endif

# ---------------------------------------------------------------------------
# Targets that need no dependencies
#
# The $(error) calls below fire while the makefile is being read, before make
# has looked at what was asked for.  So `make clean` with nothing installed
# exited 2 having removed nothing - the one command whose whole purpose is to
# work on a broken tree was the one that needed the tree to be whole.  Same
# for `docs`, `cloc` and `help`, and for `uninstall`, which removes files it
# locates through PREFIX and never asks pkg-config anything.
#
# `demo` is deliberately absent: it builds and runs a binary, so it needs the
# real check.  So are `test` and `check-symbols`.
#
# $(or $(MAKECMDGOALS),all) is load-bearing.  A bare `make` names no goal, and
# an empty MAKECMDGOALS would filter to nothing and look dependency-free -
# which would skip the check exactly when it matters most.  Substituting `all`
# keeps the bare case honest.
# ---------------------------------------------------------------------------
DEPLESS_GOALS := clean fuzz-clean docs docs-pdf cloc help uninstall uninstall-debug
ifeq ($(filter-out $(DEPLESS_GOALS),$(or $(MAKECMDGOALS),all)),)
SKIP_DEP_CHECK := 1
endif

# The Ghoti.io CUtil library supplies the generic container used by the format
# parsers. Same install-then-sibling arrangement as image below.
CUTIL_PC ?= $(SUITE)-cutil$(BRANCH)
CUTIL_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags $(CUTIL_PC) 2>/dev/null)
CUTIL_LIBS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs $(CUTIL_PC) 2>/dev/null)
ifeq ($(strip $(CUTIL_CFLAGS)),)
ifndef SKIP_DEP_CHECK
$(error ghoti.io-cutil was not found by pkg-config. Run ./bootstrap.sh in the parent folder to build and install the suite into a local prefix, then pass the same PREFIX here - or point PKG_CONFIG_PATH at the directory holding its .pc file. There is deliberately no sibling-checkout fallback: a second resolution path that only in-tree builds exercise is one that silently rots.)
endif
endif
LDFLAGS += $(CUTIL_LIBS)

# The Ghoti.io Image library supplies every image codec CJelly can load (BMP,
# PNG, JPEG). Prefer the installed package; fall back to a sibling checkout so
# the suite still builds from a fresh clone before anything is installed.
#
# Installed .pc files carry the branch suffix (ghoti.io-image-dev.pc), so the
# name asked for here has to carry it too.
IMAGE_PC ?= $(SUITE)-image$(BRANCH)
IMAGE_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags $(IMAGE_PC) 2>/dev/null)
IMAGE_LIBS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs $(IMAGE_PC) 2>/dev/null)
# Fall back when pkg-config produced nothing, or echoed an unsubstituted
# placeholder (a literal "(" is the tell).
ifeq ($(strip $(IMAGE_CFLAGS)),)
ifndef SKIP_DEP_CHECK
$(error ghoti.io-image was not found by pkg-config. Run ./bootstrap.sh in the parent folder to build and install the suite into a local prefix, then pass the same PREFIX here - or point PKG_CONFIG_PATH at the directory holding its .pc file. There is deliberately no sibling-checkout fallback: a second resolution path that only in-tree builds exercise is one that silently rots.)
endif
endif
LDFLAGS += $(IMAGE_LIBS)

# ghoti.io-model, for the OBJ and MTL parsing behind the model render node.
MODEL_PC ?= $(SUITE)-model$(BRANCH)
MODEL_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags $(MODEL_PC) 2>/dev/null)
MODEL_LIBS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs $(MODEL_PC) 2>/dev/null)
ifeq ($(strip $(MODEL_CFLAGS)),)
ifndef SKIP_DEP_CHECK
$(error ghoti.io-model was not found by pkg-config. Run ./bootstrap.sh in the parent folder to build and install the suite into a local prefix, then pass the same PREFIX here - or point PKG_CONFIG_PATH at the directory holding its .pc file. There is deliberately no sibling-checkout fallback: a second resolution path that only in-tree builds exercise is one that silently rots.)
endif
endif
LDFLAGS += $(MODEL_LIBS)

# Where the loader has to look when running the tests and the demo. When the
# libraries are installed the loader finds them through ld.so.conf and these
# extra entries are simply unused; when building against sibling checkouts
# they are what makes the binaries runnable at all.
RUNTIME_LIB_DIRS := $(APP_DIR) $(LIB_INSTALL_PATH)/$(SUITE)
# Absolute, so a recipe that cd's elsewhere first still resolves them.
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
RUNTIME_LIB_PATH := $(subst $(SPACE),:,$(strip $(abspath $(RUNTIME_LIB_DIRS))))

# The standard include directories for the project, plus the dependencies'.
# These belong here rather than in CFLAGS because every compile rule uses
# INCLUDE - the test rule among them, through TEST_INCLUDE - and a test that
# cannot include a dependency's header cannot test code that uses it.
INCLUDE := -I include/ -I $(GEN_DIR)/ $(CUTIL_CFLAGS) $(IMAGE_CFLAGS) $(MODEL_CFLAGS)

# Automatically collect all .c source files under the src directory.
# src/main.c is the demo program, not part of the library. It was landing in
# LIBOBJECTS, so the shared object carried a main() it had no use for and the
# static archive made that a duplicate-symbol error when the demo linked it.
#
# src/platform holds one directory per window system and exactly one of them
# is built. Selecting here rather than wrapping each file in #ifdef is
# deliberate: a file whose whole body is conditioned out is an empty
# translation unit, which -pedantic-errors rejects as "ISO C forbids an empty
# translation unit", so that route needs a dummy declaration in every file to
# work at all. It also means neither platform module carries a conditional.
ifeq ($(OS_NAME), Windows)
PLATFORM_NAME := win32
else
PLATFORM_NAME := x11
endif
PLATFORM_DIR := src/platform/$(PLATFORM_NAME)

PLATFORM_SOURCES := $(shell find $(PLATFORM_DIR) -type f -name '*.c' 2>/dev/null)

# A platform directory that matches nothing is the dangerous case, not a
# missing-file error: SOURCES just comes back one directory shorter, the
# library links because nothing in the unit tests calls into the event loop,
# all 118 tests pass, and the demo comes up and never responds to an event.
# Checked at parse time, where it is a hard error naming the fix. That also
# blocks `clean`, which is the known cost of a parse-time $$(error) here; the
# dependency checks above already have it, and the state that triggers this
# one is a tree somebody has broken rather than one anybody builds.
ifeq ($(strip $(PLATFORM_SOURCES)),)
$(error No platform module found in $(PLATFORM_DIR). Each window system has \
one directory under src/platform and exactly one is built; OS_NAME is \
$(OS_NAME), which selects $(PLATFORM_NAME). Add the directory, or correct \
PLATFORM_NAME above.)
endif

SOURCES := $(shell find src -type f -name '*.c' ! -name 'main.c' ! -path 'src/platform/*') \
	$(PLATFORM_SOURCES)

# Convert each source file path to an object file path.
LIBOBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SOURCES))


TESTFLAGS := `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs --cflags gtest`

# The checks `make test` runs besides the tests themselves. Named in a
# variable so that a build which cannot satisfy them can clear it: the
# coverage target does, because --coverage links the gcov runtime, whose
# mangle_path check-symbols is right to reject in a shipping library and
# wrong to reject in an instrumented one. Spelled as text's TEST_GATES is.
TEST_GATES ?= check-symbols check-stamps check-aliasing check-headers check-quiet



# The static archive, not -l: a static link resolves hidden symbols. The
# dependencies follow it, because an archive carries no DT_NEEDED of its own.
CJELLYLIBRARY := -Wl,--whole-archive $(APP_DIR)/$(STATIC_TARGET) -Wl,--no-whole-archive $(IMAGE_LIBS) $(MODEL_LIBS) $(CUTIL_LIBS)


all: $(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET) $(APP_DIR)/main$(EXE_EXTENSION) ## Build the shared and static libraries

####################################################################
# Unit Tests
####################################################################

# Discover test sources and compute an executable name for each.
# test_foo.cpp -> testFoo, matching the convention used across the suite.
UNIT_TEST_PAIRS := $(shell find tests -type f -name 'test*.cpp' 2>/dev/null | sort | while read f; do \
	echo "$$f|$$(basename "$$f" .cpp | sed 's/test_/test/; s/^test\([a-z]\)/test\U\1/')"; done)
UNIT_TEST_EXECUTABLES := $(addprefix $(APP_DIR)/,$(addsuffix $(EXE_EXTENSION),\
	$(foreach pair,$(UNIT_TEST_PAIRS),$(word 2,$(subst |, ,$(pair))))))

# Tests reach internal headers as well as the public ones, and locate their
# fixtures through CJELLY_TEST_DIR (see tests/test_helpers.h).
TEST_INCLUDE := $(INCLUDE) -I src/ -I tests/

# The stamp is a prerequisite here for the same reason it is on the object
# rules, but the reason is easy to miss: these binaries already rebuild when
# CFLAGS or CXXFLAGS move, because that rebuilds the library objects, which
# relinks the archive, which is a normal prerequisite above. That coverage is
# a side effect of two other decisions rather than something this rule asks
# for, and it does not extend to TESTFLAGS, which appears only in the recipe
# below. Without the stamp, `make test TESTFLAGS=...` rebuilt nothing at all
# and the suite went on running binaries built with the old flags.
define unit-test-rule
$(APP_DIR)/$2$(EXE_EXTENSION): $1 $(APP_DIR)/$(STATIC_TARGET) $(FLAGS_STAMP) | $(APP_DIR)/$(TARGET)
	@printf "\n### Compiling and linking %s Test ###\n" "$2"
	@mkdir -p $$(@D)
	$$(CXX) $$(CXXFLAGS) $$(TEST_INCLUDE) -MMD -MP -MF $$(@D)/$2.d -o $$@ $$< $$(CJELLYLIBRARY) $$(LDFLAGS) $$(TESTFLAGS)
endef
$(foreach pair,$(UNIT_TEST_PAIRS),$(eval $(call unit-test-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

-include $(UNIT_TEST_EXECUTABLES:%=%.d)

####################################################################
# Test Files
####################################################################

# Automatically gather all files under the test/ directory (recursively)
TEST_FILES_SRC := $(shell find test/ -type f)

# Destination files will be placed under $(APP_DIR)/ preserving the test/ folder structure.
TEST_FILES := $(addprefix $(APP_DIR)/, $(TEST_FILES_SRC))

# Pattern rule to copy each test file from the test/ directory to $(APP_DIR)/test/
$(APP_DIR)/test/%: test/%
	@mkdir -p $(dir $@)
	cp -u $< $@


####################################################################
# Dependency Inclusion
####################################################################

# Explicit list of dependency files (no wildcard: same set on all platforms, faster make startup).
DEPFILES := $(LIBOBJECTS:.o=.d)
-include $(DEPFILES)


####################################################################
# Object Files
####################################################################

# Pattern rule for C source files: compile .c files to .o files, generating dependency files.
####################################################################
# Generated version header
####################################################################

LIBVER_GEN := $(GEN_DIR)/ghoti.io/cjelly/libver_gen.h

# libver_gen.h is regenerated on every build and rewritten only when its content
# changes, so a variable given on the command line - make MAJOR_VERSION=2, or
# make BRANCH=-dev - takes effect. Keying the rule on the Makefile's timestamp
# alone left the previous token and version baked into the build, and nothing
# said so.
.PHONY: force-libver
force-libver:

$(LIBVER_GEN): force-libver
	@if [ -z "$(LIBVER_SYMBOL)" ]; then \
		printf "### LIBVER_SYMBOL is empty ###\n" >&2; exit 1; \
	fi
	@mkdir -p $(@D)
	@printf '%s\n' \
		'// Generated by the Makefile. Do not edit; see CONVENTIONS.md section 4.' \
		'#ifndef GHOTI_IO_CJ_LIBVER_GEN_H' \
		'#define GHOTI_IO_CJ_LIBVER_GEN_H' \
		'' \
		'/** The symbol namespace for this build, from the Makefile'"'"'s BRANCH. */' \
		'#define GHOTIIO_CJELLY_NAME $(LIBVER_SYMBOL)' \
		'' \
		'/** Human-readable version of this build. */' \
		'#define GHOTIIO_CJELLY_VERSION "$(VERSION_STRING)"' \
		'' \
		'/** The same version as three integers. */' \
		'#define GHOTIIO_CJELLY_VERSION_MAJOR $(MAJOR_VERSION)' \
		'#define GHOTIIO_CJELLY_VERSION_MINOR $(VERSION_MINOR_ONLY)' \
		'#define GHOTIIO_CJELLY_VERSION_PATCH $(VERSION_PATCH_ONLY)' \
		'' \
		'#endif // GHOTI_IO_CJ_LIBVER_GEN_H' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(OBJ_DIR)/%.o: src/%.c $(FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling $@ ###\n"
	@mkdir -p $(@D)
	$(CC) $(LIB_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

# Pattern rule for C++ source files (if any):
$(OBJ_DIR)/%.o: src/%.cpp $(FLAGS_STAMP)
	@printf "\n### Compiling $@ ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

# Because the shaders are generated (and therefore will not exist the first
# time that the Makefile is run), we need to explicitly add a dependency to the
# object file.  Otherwise, the shader will not be compiled yet, meaning that
# the generated header file will not exist yet, and the compiler error out
# before generating the list of dependencies.
$(OBJ_DIR)/cjelly.o: \
	$(GEN_DIR)/shaders/basic.vert.h \
	$(GEN_DIR)/shaders/basic.frag.h \
	$(GEN_DIR)/shaders/textured.frag.h \
	$(GEN_DIR)/shaders/bindless.vert.h \
	$(GEN_DIR)/shaders/bindless.frag.h \
	$(GEN_DIR)/shaders/color.vert.h \
	$(GEN_DIR)/shaders/color.frag.h \
	$(GEN_DIR)/shaders/blur.vert.h \
	$(GEN_DIR)/shaders/blur.frag.h \
	$(GEN_DIR)/shaders/textured_simple.frag.h

# rgraph.o dependencies are now handled automatically by the circular dependency approach


####################################################################
# Shaders
####################################################################

# Compute the desired variable name from the source file.
# Replace dots with underscores.
VAR_NAME = $(subst .,_, $(notdir $<))

# Automatically discover all shader files and generate their headers
SHADER_SOURCES := $(shell find src/shaders -name "*.vert" -o -name "*.frag" -o -name "*.comp")
SHADER_HEADERS := $(patsubst src/shaders/%,$(GEN_DIR)/shaders/%.h,$(SHADER_SOURCES))

SHADER_SPV := $(patsubst src/shaders/%,$(APP_DIR)/shaders/%.spv,$(SHADER_SOURCES))

# The .spv files are only ever needed to build the generated headers, so make
# treats them as intermediate and deletes them once the headers exist. On the
# next build the headers look up to date while their inputs are gone, and any
# regeneration runs xxd against a missing file and writes an empty header --
# which then fails to compile with "'color_vert_spv' undeclared". Keep them.
.SECONDARY: $(SHADER_SPV)
.PRECIOUS: $(SHADER_SPV)

# Phony target to ensure all shader headers are generated before any object files
.PHONY: shader-headers
shader-headers: $(SHADER_HEADERS)

# Every object must wait for the headers, not just rgraph.o: engine.c and
# cjelly.c include them too, so a parallel build could compile either before
# the headers exist. Depend on the header files themselves rather than the
# phony target, which would be out of date on every run and force a full
# rebuild each time.
$(LIBOBJECTS): | $(SHADER_HEADERS)

# Pattern rule to generate a header file from a SPIR-V file.
$(GEN_DIR)/shaders/%.h: $(APP_DIR)/shaders/%.spv
	@printf "\n### Generating $@ ###\n"
	@mkdir -p $(@D)
	xxd -i $< \
	  | sed "s/^\(unsigned char \)[^[]*\(\[.*\)/\1$(VAR_NAME)\2/" \
	  | sed "s/^\(unsigned int \)[^ ]*/\1$(VAR_NAME)_len/" > $@

# Pattern rule to compile shaders to SPIR-V
$(APP_DIR)/shaders/%.spv: src/shaders/%
	@printf "\n### Compiling $@ ###\n"
	@mkdir -p $(@D)
	glslangValidator -V $< -o $@

# Windows-specific shader compilation (fallback if glslangValidator not available)
ifeq ($(OS),Windows_NT)
# Check if glslangValidator is available
GLSLANG_AVAILABLE := $(shell which glslangValidator 2>/dev/null)
ifeq ($(GLSLANG_AVAILABLE),)
# TODO(windows): this writes an empty shader header rather than failing, so
# the build succeeds and every pipeline creation fails at run time with
# nothing pointing at the cause. It should fail at this step instead, naming
# the missing tool. See WINDOWS-TODO.md item 3.
# If glslangValidator is not available, create empty shader headers
$(GEN_DIR)/shaders/%.h: $(APP_DIR)/shaders/%.spv
	@printf "\n### Generating empty $@ ###\n"
	@mkdir -p $(@D)
	@echo "// Empty shader header - glslangValidator not available on Windows" > $@
	@echo "static const unsigned char $(VAR_NAME)[] = {0};" >> $@
	@echo "static const unsigned int $(VAR_NAME)_len = 0;" >> $@

$(APP_DIR)/shaders/%.spv: src/shaders/%
	@printf "\n### Warning: glslangValidator not found, creating empty shader $@ ###\n"
	@mkdir -p $(@D)
	@echo "// Empty shader - glslangValidator not available on Windows" > $@
endif
endif


####################################################################
# Shared Library
####################################################################

$(APP_DIR)/$(STATIC_TARGET): $(LIBOBJECTS)
	@printf "\n### Archiving CJelly Library ###\n"
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

# The objects are named rather than spelled $^ because the flags stamp is a
# prerequisite here, and $^ would hand the stamp file to the linker as though
# it were an object.
#
# The stamp is needed because this recipe expands
# $(OS_SPECIFIC_LIBRARY_NAME_FLAG), which no object recipe expands and no
# stamp recorded. Every other variable on this line reaches the library
# through the objects - they depend on the stamp, so a change to CXXFLAGS or
# LDFLAGS rebuilds them and the relink follows - which is coverage inherited
# from another rule's prerequisites rather than asked for here, and it does
# not extend to a variable only this recipe uses. Measured: with the soname
# flag changed on the command line, `make` rebuilt 0 of 33 artifacts and the
# library kept its old SONAME, while a forced relink under the same flag
# writes the new one and a forced relink without it writes the old one back.
$(APP_DIR)/$(TARGET): \
		$(LIBOBJECTS) $(FLAGS_STAMP)
	@printf "\n### Compiling CJelly Library ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(LIBOBJECTS) $(LDFLAGS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)

ifeq ($(OS_NAME), Linux)
	@ln -f -s $(TARGET) $(APP_DIR)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(APP_DIR)/$(BASE_NAME)
endif

####################################################################
# Unit Tests
####################################################################

# $(APP_DIR)/test$(EXE_EXTENSION): \
# 				test/test.cpp \
# 				$(DEP_CJELLY) \
# 				$(APP_DIR)/$(TARGET)
# 	@printf "\n### Compiling CJelly Test ###\n"
# 	@mkdir -p $(@D)
# 	$(CXX) $(CXXFLAGS) $(INCLUDE) -o $@ $< $(LDFLAGS) $(TESTFLAGS) $(CJELLYLIBRARY)

$(APP_DIR)/main$(EXE_EXTENSION): \
		src/main.c \
		$(DEP_CJELLY) \
		$(DEP_FORMAT_3D_MTL) \
		$(DEP_FORMAT_3D_OBJ) \
		$(APP_DIR)/$(TARGET) \
		$(FLAGS_STAMP)
	@printf "\n### Compiling CJelly Test ###\n"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(CJELLYLIBRARY) $(LDFLAGS)

####################################################################
# Commands
####################################################################

# General commands
.PHONY: clean cloc docs docs-pdf coverage
.PHONY: fuzz fuzz-clean test-asan test-ubsan
# Release build commands
.PHONY: all demo install test test-watch uninstall watch check-symbols check-stamps check-aliasing
.PHONY: check-headers check-quiet
# Debug build commands
.PHONY: all-debug install-debug test-debug test-watch-debug uninstall-debug watch-debug


watch: ## Watch the file directory for changes and compile the target
	@while true; do \
		make --no-print-directory all; \
		printf "\033[0;32m\n"; \
		printf "#########################\n"; \
		printf "# Waiting for changes.. #\n"; \
		printf "#########################\n"; \
		printf "\033[0m\n"; \
		inotifywait -qr -e modify -e create -e delete -e move src include bison flex test Makefile --exclude '/\.'; \
		done

test-watch: ## Watch the file directory for changes and run the unit tests
	@while true; do \
		make --no-print-directory all; \
		make --no-print-directory test; \
		printf "\033[0;32m\n"; \
		printf "#########################\n"; \
		printf "# Waiting for changes.. #\n"; \
		printf "#########################\n"; \
		printf "\033[0m\n"; \
		inotifywait -qr -e modify -e create -e delete -e move src include bison flex test Makefile --exclude '/\.'; \
		done

####################################################################
# Symbol namespace check
####################################################################

# The .so check above is the wrong instrument for internal names, because
# -fvisibility=hidden keeps every one of them out of it. The static archive
# is what the tests link and what a consumer choosing static linkage gets,
# and there every non-static definition is a name in the global namespace.
# `display` sat there, so did select_xinput2_events, and so did four
# functions with no callers at all - none of which the .so could show.
#
# The generated SPIR-V arrays are the exception and are pinned rather than
# excused: the shader generator gives them external linkage, which is why
# including one of those headers in a second translation unit is a
# duplicate-symbol error. Fixing that is a change to the generator.
CHECK_ARCHIVE_SHADER_SYMBOLS := 26

check-symbols: ## Fail if any exported symbol lacks the version namespace
check-symbols: $(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET)
ifeq ($(OS_NAME), Linux)
	@leaked=$$(nm -D --defined-only $(APP_DIR)/$(TARGET) \
		| awk '$$2 ~ /^[TDBR]$$/ {print $$3}' \
		| grep -v '^$(LIBVER_SYMBOL)_' | grep -v '^_' || true); \
	if [ -n "$$leaked" ]; then \
		printf "\033[0;31m\n### Exported symbols missing the $(LIBVER_SYMBOL)_ namespace ###\033[0m\n" >&2; \
		printf "%s\n" "$$leaked" >&2; \
		printf "\nEach needs a '#define <name> GHOTIIO_CJELLY(<name>)' line in namespace.h, or\n" >&2; \
		printf "should not be exported. See CONVENTIONS.md section 4.\n" >&2; \
		exit 1; \
	fi
	@shaders=$$(nm --defined-only $(APP_DIR)/$(STATIC_TARGET) \
		| awk '$$2 ~ /^[TDBRG]$$/ {print $$3}' \
		| grep -cE '_spv(_len)?$$' || true); \
	bare=$$(nm --defined-only $(APP_DIR)/$(STATIC_TARGET) \
		| awk '$$2 ~ /^[TDBRG]$$/ {print $$3}' \
		| grep -vE '^($(LIBVER_SYMBOL)_|cj_|CJelly|cjelly)' \
		| grep -vE '_spv(_len)?$$' | grep -v '^_' | sort -u || true); \
	if [ "$$shaders" != "$(CHECK_ARCHIVE_SHADER_SYMBOLS)" ]; then \
		printf "\033[0;31mcheck-symbols: the archive has %s generated shader symbols, not the %s pinned. If a shader was added the pin wants raising; if it fell to 0 this scan has stopped matching and the check below means nothing.\033[0m\n" "$$shaders" "$(CHECK_ARCHIVE_SHADER_SYMBOLS)" >&2; \
		exit 1; \
	fi; \
	if [ -n "$$bare" ]; then \
		printf "\033[0;31m\n### Archive symbols with no library prefix ###\033[0m\n" >&2; \
		printf "%s\n" "$$bare" >&2; \
		printf "\nThese are invisible in the .so and real in the archive, where they\n" >&2; \
		printf "collide with whatever else the consumer links. Give it internal\n" >&2; \
		printf "linkage if one file uses it, a cj_ prefix if several do.\n" >&2; \
		exit 1; \
	fi
	@split=$$(nm -D --undefined-only $(APP_DIR)/$(TARGET) \
		| awk '{print $$2}' | grep '^$(LIBVER_SYMBOL)_' || true); \
	if [ -n "$$split" ]; then \
		printf "\033[0;31m\n### Renamed but undefined - a split symbol ###\033[0m\n" >&2; \
		printf "%s\n" "$$split" >&2; \
		exit 1; \
	fi
	# *_internal.h is excluded: it declares internals, which are hidden on
	# purpose. cjelly keeps those under include/ rather than src/ as the other
	# libraries do; see CONVENTIONS.md section 13.
	@unexported=$$(find include -name '*.h' ! -name '*_internal.h' -exec awk '/^#if DOXYGEN/{d=1} d==0 && /^[a-z_][A-Za-z0-9_ ]*\**[[:space:]]*cj_[a-z0-9_]+[[:space:]]*\(/{print FILENAME": "$$0} /^#endif/{d=0}' {} + \
		| grep -vE 'typedef|static inline' || true); \
	if [ -n "$$unexported" ]; then \
		printf "\033[0;31m\n### Public declarations without CJ_API ###\033[0m\n" >&2; \
		printf "%s\n" "$$unexported" >&2; \
		printf "\nThe library builds with -fvisibility=hidden, so these are not exported\n" >&2; \
		printf "and a consumer linking the .so gets an undefined reference. The tests\n" >&2; \
		printf "link the archive and would not notice.\n" >&2; \
		exit 1; \
	fi
	@nomacros=$$(find include src -name '*.h' \
		! -name 'libver.h' ! -name 'libver_gen.h' ! -name 'namespace.h' ! -name 'macros.h' \
		-exec grep -L '#include <ghoti.io/cjelly/macros.h>' {} + || true); \
	if [ -n "$$nomacros" ]; then \
		printf "\033[0;31m\n### Headers that do not include macros.h ###\033[0m\n" >&2; \
		printf "%s\n" "$$nomacros" >&2; \
		printf "\nEvery header must include <ghoti.io/cjelly/macros.h> before it declares\n" >&2; \
		printf "anything, so that the renames in namespace.h are already in effect. A\n" >&2; \
		printf "header that skips it can name a type before that type has been renamed,\n" >&2; \
		printf "producing two different types under one spelling.\n" >&2; \
		printf "See CONVENTIONS.md section 4.\n" >&2; \
		exit 1; \
	fi
# An include guard is `#ifndef X` IMMEDIATELY followed by `#define X`, and
# the scan below requires both. Taking the first #ifndef in the file was
# enough while every header opened with one, and is wrong for a header that
# uses `#pragma once` and then has an ordinary conditional: the first
# `#ifndef _WIN32` in such a file was reported as a guard named _WIN32, with
# the wrong prefix and shared by every header that did the same. The guard a
# header actually has is the pair, so the scan looks for the pair.
	@badguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0;prev=""} !d && prev ~ /^#ifndef[ \t]/ && /^#define[ \t]/ {split(prev,a," ");split($$0,b," "); if (a[2]==b[2]) {print a[2]; d=1}} {prev=$$0}' {} + \
		| awk '$$1 !~ /^GHOTI_IO_CJ_/ {print $$1}' || true); \
	if [ -n "$$badguards" ]; then \
		printf "\033[0;31m\n### Include guards with the wrong prefix ###\033[0m\n" >&2; \
		printf "%s\n" "$$badguards" >&2; \
		printf "\nGuards mirror the path: GHOTI_IO_CJ_<PATH>_H. A guard without the\n" >&2; \
		printf "library token is one rename away from colliding with another library's.\n" >&2; \
		exit 1; \
	fi
	@dupguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0;prev=""} !d && prev ~ /^#ifndef[ \t]/ && /^#define[ \t]/ {split(prev,a," ");split($$0,b," "); if (a[2]==b[2]) {print a[2]; d=1}} {prev=$$0}' {} + \
		| sort | uniq -d || true); \
	if [ -n "$$dupguards" ]; then \
		printf "\033[0;31m\n### Headers sharing an include guard ###\033[0m\n" >&2; \
		printf "%s\n" "$$dupguards" >&2; \
		printf "\nTwo headers with one guard means whichever is included second is\n" >&2; \
		printf "silently empty. Guards mirror the path: GHOTI_IO_CJ_<PATH>_H.\n" >&2; \
		exit 1; \
	fi
	@printf "\033[0;32mEvery exported symbol carries the $(LIBVER_SYMBOL)_ namespace.\033[0m\n"
	@printf "\033[0;32mEvery public declaration carries CJ_API.\033[0m\n"
	@printf "\033[0;32mEvery archive symbol carries a library prefix, the $(CHECK_ARCHIVE_SHADER_SYMBOLS) generated shader symbols excepted, as pinned.\033[0m\n"
	@printf "\033[0;32mEvery header includes macros.h.\033[0m\n"
	@printf "\033[0;32mEvery include guard is unique and correctly prefixed.\033[0m\n"
else
	@printf "check-symbols: skipped (Linux only)\n"
endif

test: ## Make and run the Unit tests
test: \
		$(TEST_FILES) \
		$(APP_DIR)/$(TARGET) \
		$(UNIT_TEST_EXECUTABLES) \
		$(TEST_GATES)
	@for test_exe in $(UNIT_TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf "\033[0;32m\n"; \
		printf "############################\n"; \
		printf "### Running %s\n" "$$test_name"; \
		printf "############################\n"; \
		printf "\033[0m\n"; \
		LD_LIBRARY_PATH="$(RUNTIME_LIB_PATH)" CJELLY_TEST_DIR="$(CURDIR)/test" \
			$$test_exe --gtest_brief=1 || exit 1; \
	done

# An .obj file for the demo's model window: make demo MODEL=path/to/model.obj
# Made absolute because the demo runs from $(APP_DIR), not from here, so a
# relative path would be resolved against the wrong directory.
MODEL ?=
DEMO_MODEL_ARG := $(if $(MODEL),$(abspath $(MODEL)),)

demo: ## Build and run the interactive Vulkan demo (needs a display). MODEL=x.obj to choose a model.
demo: \
		$(TEST_FILES) \
		$(APP_DIR)/$(TARGET) \
		$(APP_DIR)/main$(EXE_EXTENSION)
	@printf "\033[0;32m\n"
	@printf "############################\n"
	@printf "### Running the demo     ###\n"
	@printf "############################\n"
	@printf "\033[0m\n"
	@if [ -n "$(DEMO_MODEL_ARG)" ] && [ ! -f "$(DEMO_MODEL_ARG)" ]; then \
		printf "\033[0;31mNo such file: $(DEMO_MODEL_ARG)\033[0m\n"; \
		exit 1; \
	fi
	cd $(APP_DIR) && LD_LIBRARY_PATH="$(RUNTIME_LIB_PATH)" $(ENV_VARS) ./main$(EXE_EXTENSION) $(DEMO_MODEL_ARG)

####################################################################
# Sanitizer build (ASan + UBSan)
####################################################################
# Instrumented objects go in a sibling of the ordinary build directory rather
# than a child, so that nothing can mistake one for the other and `clean` can
# name both. The generated headers are shared deliberately: the shaders and
# the version header do not depend on how the library is compiled, and
# INCLUDE already points at them.
#
# float-cast-overflow is named explicitly, and named twice. GCC does not put
# it in the `undefined` group - clang does, which is where the expectation
# comes from - and -fno-sanitize-recover=undefined names that same group, so
# it does not cover the check either. Enable only `undefined` and a
# float-to-integer overflow prints a diagnostic and the run still exits 0: a
# gate that describes the bug and passes. Spelling the list once and using it
# in both flags is what keeps the two from drifting apart again.
UBSAN_CHECKS := undefined,float-cast-overflow
# -O1 is pinned here rather than inherited, and that is a decision, not a
# default. It appends after CFLAGS and the last -O on the line wins, so the
# 15 C translation units of this target compile at -O1 whatever the release
# level is; measured, not assumed. The argument the other way is real - a
# gate that runs at the level that ships is testing the code that ships - but
# an inherited level means `make test-asan` silently changes what it tests
# every time the release level moves, and a gate that quietly redefines
# itself is worth less than one that is slightly less faithful. The fuzz
# target already pins its own -O1 for the same reason.
ASAN_UBSAN_FLAGS := -fsanitize=address,$(UBSAN_CHECKS) \
	-fno-sanitize-recover=$(UBSAN_CHECKS) -fno-omit-frame-pointer -g -O1

ASAN_BUILD_DIR := ./build/$(BUILD)-asan
ASAN_OBJ_DIR := $(ASAN_BUILD_DIR)/objects
ASAN_FLAGS_STAMP := $(ASAN_OBJ_DIR)/.flags
ASAN_APP_DIR := $(ASAN_BUILD_DIR)/apps

ASAN_LIBOBJECTS := $(patsubst src/%.c,$(ASAN_OBJ_DIR)/%.o,$(SOURCES))
ASAN_STATIC_TARGET := $(BASE_NAME_PREFIX)-asan.a

ASAN_CFLAGS := $(LIB_CFLAGS) $(ASAN_UBSAN_FLAGS)
ASAN_CXXFLAGS := $(CXXFLAGS) $(ASAN_UBSAN_FLAGS)
ASAN_LDFLAGS := $(LDFLAGS) $(ASAN_UBSAN_FLAGS)

# The tests link the instrumented archive, exactly as the ordinary ones link
# the ordinary archive. That also means there is no ASan runtime ordering
# problem to solve: a static link puts libasan in the executable's own NEEDED
# list, so it initialises before anything it has to intercept, and no
# LD_PRELOAD is needed.
ASAN_CJELLYLIBRARY := -Wl,--whole-archive $(ASAN_APP_DIR)/$(ASAN_STATIC_TARGET) \
	-Wl,--no-whole-archive $(IMAGE_LIBS) $(MODEL_LIBS) $(CUTIL_LIBS)

$(ASAN_LIBOBJECTS): | $(SHADER_HEADERS)

$(ASAN_OBJ_DIR)/%.o: src/%.c $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling ASan $@ ###\n"
	@mkdir -p $(@D)
	$(CC) $(ASAN_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_APP_DIR)/$(ASAN_STATIC_TARGET): $(ASAN_LIBOBJECTS)
	@printf "\n### Archiving ASan CJelly Library ###\n"
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

# The archive is a normal prerequisite, not an order-only one, for the same
# reason it is in the ordinary test rule: it is what the recipe links, so a
# change to library source has to relink the test. Behind a `|` the suite
# would keep passing against the previous build.
define asan-unit-test-rule
$(ASAN_APP_DIR)/$2$(EXE_EXTENSION): $1 $(ASAN_APP_DIR)/$(ASAN_STATIC_TARGET) $(ASAN_FLAGS_STAMP)
	@printf "\n### Compiling and linking ASan %s Test ###\n" "$2"
	@mkdir -p $$(@D)
	$$(CXX) $$(ASAN_CXXFLAGS) $$(TEST_INCLUDE) -MMD -MP -MF $$(@D)/$2.d -o $$@ $$< $$(ASAN_CJELLYLIBRARY) $$(ASAN_LDFLAGS) $$(TESTFLAGS)
endef
$(foreach pair,$(UNIT_TEST_PAIRS),$(eval $(call asan-unit-test-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

ASAN_TEST_EXECUTABLES := $(addprefix $(ASAN_APP_DIR)/,$(addsuffix $(EXE_EXTENSION),\
	$(foreach pair,$(UNIT_TEST_PAIRS),$(word 2,$(subst |, ,$(pair))))))

-include $(ASAN_TEST_EXECUTABLES:%=%.d)

# check-symbols is deliberately not a prerequisite. It inspects
# $(APP_DIR)/$(TARGET), which this target does not build, so depending on it
# would link a release shared library as a side effect of asking for an
# instrumented run - to re-check what `make test` already checked.
test-asan: ## Run the unit tests under AddressSanitizer + UndefinedBehaviorSanitizer (Linux only)
test-asan: $(ASAN_TEST_EXECUTABLES)
ifeq ($(OS_NAME), Linux)
	@printf "\033[0;36m\n"
	@printf "###########################################\n"
	@printf "### Running tests with ASan + UBSan     ###\n"
	@printf "###########################################\n"
	@printf "\033[0m\n"
	@for test_exe in $(ASAN_TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf "\033[0;30;43m\n### Running %s (ASan+UBSan) ###\033[0m\n\n" "$$test_name"; \
		env -u LD_PRELOAD \
			LD_LIBRARY_PATH="$(RUNTIME_LIB_PATH)" \
			CJELLY_TEST_DIR="$(CURDIR)/test" \
			ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
			UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
			$$test_exe --gtest_brief=1 || exit 1; \
	done
	@printf "\033[0;32m\nAll tests passed with ASan + UBSan.\033[0m\n"
else
	@printf "\033[0;31mSanitizer builds are currently only supported on Linux.\033[0m\n"
	@exit 1
endif

# LD_PRELOAD is cleared for the run above, not as tidiness. This desktop sets
# it to libgtk3-nocsd, which is loaded before libasan and displaces the
# interceptors; the symptom is a sanitizer that reports nothing, which is
# indistinguishable from a clean run.

test-ubsan: ## Alias for test-asan (ASan and UBSan run together)
test-ubsan: test-asan

####################################################################
# Fuzzing (libFuzzer)
####################################################################
#
# Only the mesh post-processing is fuzzed, and only it is compiled here. The
# rest of the library needs Vulkan and a display; none of it parses anything,
# and building it under clang to reach code the harness never calls would buy
# nothing but build time.
#
# The parse itself belongs to Ghoti.io Model and is fuzzed there. What these
# harnesses cover is everything cjelly does afterwards - range checking face
# indices, fan triangulation, generated normals, the V flip and the bounds -
# which is cjelly's own code operating on numbers a file chose.
#
# The sources are rebuilt with -fsanitize=fuzzer-no-link rather than linked
# from the ordinary library: libFuzzer steers by the coverage it observes, and
# a harness over an uninstrumented library sees no branches and degenerates
# into random input.
FUZZ_CC ?= clang
FUZZ_CXX ?= clang++
FUZZ_CC_OK := $(shell which $(FUZZ_CC) 2>/dev/null)
FUZZ_SAN := -fsanitize=address,$(UBSAN_CHECKS) \
	-fno-sanitize-recover=$(UBSAN_CHECKS) -fno-omit-frame-pointer -g -O1
FUZZ_LIB_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer-no-link
FUZZ_BIN_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer
FUZZ_DIR := $(BUILD_DIR)-fuzz
FUZZ_OBJ_DIR := $(FUZZ_DIR)/objects
FUZZ_FLAGS_STAMP := $(FUZZ_OBJ_DIR)/.flags
FUZZ_APP_DIR := $(FUZZ_DIR)/apps
# The tracked seeds, read only. libFuzzer writes what it discovers into a
# working corpus under the build directory instead, because a campaign
# adds thousands of machine-named files and a repository is the wrong
# place for them: they would drown every later `git status`, and they are
# reproducible from the seeds and the harness in the time it takes to
# read them. Seeds here are hand-written and each one is a case worth
# keeping - see tests/fuzz/README.md.
FUZZ_CORPUS := tests/fuzz/corpus
FUZZ_WORK := $(FUZZ_DIR)/corpus

# What the harnesses actually reach: the mesh builder and the allocator it
# asks for memory through.
FUZZ_SOURCES := src/format/3d/mesh.c src/allocator.c
FUZZ_OBJECTS := $(patsubst src/%.c,$(FUZZ_OBJ_DIR)/%.o,$(FUZZ_SOURCES))

# A smoke-test length by default; for a real campaign: make fuzz FUZZ_TIME=3600
FUZZ_TIME ?= 60

$(FUZZ_OBJECTS): | $(LIBVER_GEN)

$(FUZZ_OBJ_DIR)/%.o: src/%.c $(FUZZ_FLAGS_STAMP)
	@mkdir -p $(@D)
	@$(FUZZ_CC) $(FUZZ_LIB_FLAGS) -std=c17 -w -DCJELLY_BUILD $(INCLUDE) -c $< -o $@

# $1 = harness basename (fuzz_mesh), $2 = target suffix (mesh)
define fuzz-rule
fuzz-$2: ## Build the $2 fuzz harness (requires clang)
fuzz-$2: $$(FUZZ_APP_DIR)/$1

$$(FUZZ_APP_DIR)/$1: tests/fuzz/$1.cpp tests/fuzz/fuzz_mesh_invariants.h $$(FUZZ_OBJECTS) \
		$$(FUZZ_FLAGS_STAMP)
	@if [ -z "$$(FUZZ_CC_OK)" ]; then \
		echo "fuzzing requires $$(FUZZ_CXX); install clang or set FUZZ_CC/FUZZ_CXX"; \
		exit 1; \
	fi
	@mkdir -p $$(@D) $$(FUZZ_CORPUS)/$2
	@printf "\n### Building fuzz harness: $1 ###\n"
	$$(FUZZ_CXX) $$(FUZZ_BIN_FLAGS) -std=c++20 -w $$(INCLUDE) -I tests/fuzz \
		-o $$@ $$< $$(FUZZ_OBJECTS) $$(MODEL_LIBS) $$(CUTIL_LIBS)

fuzz-run-$2: ## Run the $2 fuzzer for $$(FUZZ_TIME) seconds
fuzz-run-$2: $$(FUZZ_APP_DIR)/$1
	@mkdir -p $$(FUZZ_WORK)/$2 $$(FUZZ_CORPUS)/$2
	@printf "\n### Fuzzing $2 for $$(FUZZ_TIME)s ###\n"
	@env -u LD_PRELOAD LD_LIBRARY_PATH="$$(RUNTIME_LIB_PATH)" \
		$$(FUZZ_APP_DIR)/$1 $$(FUZZ_WORK)/$2 $$(FUZZ_CORPUS)/$2 \
		-artifact_prefix=$$(FUZZ_DIR)/ \
		-max_total_time=$$(FUZZ_TIME) -print_final_stats=1
endef

$(eval $(call fuzz-rule,fuzz_mesh,mesh))
$(eval $(call fuzz-rule,fuzz_mesh_struct,mesh-struct))

fuzz: ## Build and run every fuzzer for $(FUZZ_TIME) seconds each
fuzz: fuzz-run-mesh fuzz-run-mesh-struct

fuzz-clean: ## Remove the fuzz build (keeps the corpus)
	-@rm -rf $(FUZZ_DIR)

clean: ## Remove all contents of the build directories.
# The sanitizer tree goes too. It is a sibling of the ordinary build
# directory rather than a child, so a clean naming only the latter leaves
# instrumented objects behind - and those are the worst kind to leave,
# because they still run.
	-@rm -rvf $(BUILD_DIR) $(ASAN_BUILD_DIR) $(FUZZ_DIR)

# Files will be as follows:
# /usr/local/lib/(SUITE)/
#   lib(SUITE)-(PROJECT)(BRANCH).so.(MAJOR).(MINOR)
#   lib(SUITE)-(PROJECT)(BRANCH).so.(MAJOR) link to previous
#   lib(SUITE)-(PROJECT)(BRANCH).so link to previous
# Where the dynamic loader configuration fragment goes. Overridable so a
# staged or user-prefix install has somewhere to write it; the default is the
# system location, which is what an ordinary `sudo make install` uses.
LDCONF_INSTALL_PATH ?= /etc/ld.so.conf.d

# What goes in the .pc Requires: field. Built from the same variables the
# compile uses, so a dependency on another branch cannot be named one way for
# the build and another way for consumers.
PC_REQUIRES := vulkan $(IMAGE_PC) $(MODEL_PC) $(CUTIL_PC)

# Where this project's own .pc file is installed. Defaults to the directory
# pkg-config is already being told to search, but separate from it so a
# staged install can write somewhere else without also redirecting lookups.
PKGCONFIG_INSTALL_PATH ?= $(PKG_CONFIG_PATH)

# $(LDCONF_INSTALL_PATH)/(SUITE)-(PROJECT)(BRANCH).conf will point to $(LIB_INSTALL_PATH)/(SUITE)
# /usr/local/include/(SUITE)/(PROJECT)(BRANCH)
#   *.h copied from ./include/(PROJECT)
# /usr/local/share/pkgconfig
#   (SUITE)-(PROJECT)(BRANCH).pc created

install: ## Install the library globally, requires sudo
# Depends on all: install used to copy whatever happened to be in the build
# directory, so it could install a stale artifact or fail outright on a clean
# tree.
install: all
	# Installing the shared library.
	@mkdir -p $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
# Install the .so file
	@cp $(APP_DIR)/$(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/
	@ln -f -s $(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)
	# Installing the ld configuration file.
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then mkdir -p $(LDCONF_INSTALL_PATH); fi
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then echo "$(LIB_INSTALL_PATH)/$(SUITE)" > $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf; fi
endif
ifeq ($(OS_NAME), Windows)
# The .dll file and the .dll.a file
	@mkdir -p $(BIN_INSTALL_PATH)/$(SUITE)
	@cp $(APP_DIR)/$(TARGET).a $(LIB_INSTALL_PATH)
	@cp $(APP_DIR)/$(TARGET) $(BIN_INSTALL_PATH)
endif
	# Installing the headers.
	#
	# Copied recursively: a flat glob of include/cjelly/*.h left out the whole
	# format/ tree, so an installed CJelly could not compile anything that
	# included cjelly/format/image.h. The generated directory holds shaders
	# rather than headers, and a glob over it failed the install outright when
	# it matched nothing.
	# Removed first: this directory is owned entirely by this project and
	# branch, and copying over the top of it would leave headers behind that
	# have since been renamed or deleted.
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@mkdir -p $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@cd include && find . -name "*.h" -exec cp --parents '{}' $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ \;
	@if [ -d "$(GEN_DIR)" ]; then \
		cd $(GEN_DIR) && find . -name "*.h" -exec cp --parents '{}' $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ \; ; \
	fi
	# Installing the pkg-config files.
	@mkdir -p $(PKGCONFIG_INSTALL_PATH)
	@cat pkgconfig/$(SUITE)-$(PROJECT).pc | sed 's/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g; s/(VERSION)/$(VERSION)/g; s|(LIB)|$(LIB_INSTALL_PATH)|g; s|(INCLUDE)|$(INCLUDE_INSTALL_PATH)|g; s|(REQUIRES)|$(PC_REQUIRES)|g' > $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
ifeq ($(OS_NAME), Linux)
	# Running ldconfig.
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then ldconfig >> /dev/null 2>&1; fi
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) installed"

uninstall: ## Delete the globally-installed files.  Requires sudo.
	# Deleting the shared library.
ifeq ($(OS_NAME), Linux)
	@rm -f $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)*
	# Deleting the ld configuration file.
	@rm -f $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf
endif
ifeq ($(OS_NAME), Windows)
	@rm -f $(LIB_INSTALL_PATH)/$(TARGET).a
	@rm -f $(BIN_INSTALL_PATH)/$(TARGET)
endif
	# Deleting the headers.
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	# Deleting the pkg-config files.
	@rm -f $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
	# Cleaning up (potentially) no longer needed directories.
	@rmdir --ignore-fail-on-non-empty $(INCLUDE_INSTALL_PATH)/$(SUITE)
	@rmdir --ignore-fail-on-non-empty $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
	# Running ldconfig.
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then ldconfig >> /dev/null 2>&1; fi
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) has been uninstalled"

debug: ## Build the project in DEBUG mode
	make all BUILD=debug

install-debug: ## Install the DEBUG library globally, requires sudo
	make install BUILD=debug

uninstall-debug: ## Delete the DEBUG globally-installed files.  Requires sudo.
	make uninstall BUILD=debug

test-debug: ## Make and run the Unit tests in DEBUG mode
	make test BUILD=debug

watch-debug: ## Watch the file directory for changes and compile the target in DEBUG mode
	make watch BUILD=debug

test-watch-debug: ## Watch the file directory for changes and run the unit tests in DEBUG mode
	make test-watch BUILD=debug

docs: ## Generate the documentation in the ./docs subdirectory
	doxygen

docs-pdf: docs ## Generate the documentation as a pdf, at ./docs/(SUITE)-(PROJECT)(BRANCH).pdf
	cd ./docs/latex/ && make
	mv -f ./docs/latex/refman.pdf ./docs/$(SUITE)-$(PROJECT)$(BRANCH)-docs.pdf

cloc: ## Count the lines of code used in the project
	cloc src include test Makefile

coverage: ## Build instrumented, run the tests, and report line coverage
# Cleans first because the object files would otherwise be reused without the
# instrumentation, then cleans and rebuilds at the end: leaving the
# instrumented objects behind would have a later `make` silently link them,
# and leaving the tree cleaned would break any sibling project that links
# this one. The cost is one extra build; coverage is not run often.
	@$(MAKE) --no-print-directory clean > /dev/null
# The instrumented build, the report and the restoration of the tree are one
# shell command so that the cleanup runs whatever fails. Letting a failure
# stop the recipe leaves the --coverage objects in build/, and the next
# ordinary `make` links them into a library that needs the gcov runtime; every
# later build then fails with undefined references to __gcov_init until
# somebody works out why.
#
# TEST_GATES is cleared because --coverage links the gcov runtime, which
# exports mangle_path. check-symbols is right to reject that in a shipping
# build and wrong to reject it here, and it made this target fail before it
# ever produced a report.
	@status=0; \
	$(MAKE) --no-print-directory test TEST_GATES= \
		EXTRA_CFLAGS="--coverage -O0" \
		EXTRA_LDFLAGS="--coverage" > /dev/null || status=$$?; \
	if [ $$status -eq 0 ]; then \
		tools/coverage.sh $(OBJ_DIR) || status=$$?; \
	else \
		printf "coverage: the instrumented test run failed; no report\n" >&2; \
	fi; \
	$(MAKE) --no-print-directory clean > /dev/null; \
	$(MAKE) --no-print-directory all > /dev/null; \
	exit $$status

help: ## Display this help
# Scan only this makefile. $(MAKEFILE_LIST) grows to include every generated
# .d file once the project has been built, and grep prefixes each match with
# a filename when given more than one file - so every target name in the
# output became "Makefile".
	@grep -E '^[ a-zA-Z_-]+:.*?## .*$$' $(firstword $(MAKEFILE_LIST)) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "%-15s %s\n", $$1, $$2}' | sed "s/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g"

valgrind: all ## Run main under valgrind with suppressions
	cd $(APP_DIR) && LD_LIBRARY_PATH=./ valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --suppressions=$(abspath tools/valgrind.supp) ./main$(EXE_EXTENSION)


####################################################################
# Flag stamps
####################################################################
# Each build tree carries the flag string it was built with. The stamp is
# rewritten only when that string differs -- written to a scratch file,
# compared, moved into place only on a difference -- so its mtime moves on a
# flag change and on nothing else. The object rules above depend on it.
#
# This replaces listing `Makefile` as a prerequisite, which was too broad (a
# comment-only edit recompiled everything) and too narrow (a command-line
# override such as `make EXTRA_CFLAGS=-O2` changes no file's mtime and so was
# invisible).
#
# These rules sit at the end of the file for two reasons. A rule's target
# expands when make reads the line, so a stamp rule above its own OBJ_DIR
# definition has an empty target: not an error, just a rule that silently does
# not exist. And the first target in a makefile is the default goal, so a stamp
# rule above `all:` makes a bare `make` build the stamp and nothing else.
.PHONY: force-flags

$(FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(LIB_CFLAGS) $(CFLAGS) $(CXXFLAGS) $(LDFLAGS) $(INCLUDE) $(TEST_INCLUDE) $(CJELLYLIBRARY) $(TESTFLAGS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(ASAN_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(ASAN_CFLAGS) $(ASAN_CXXFLAGS) $(ASAN_LDFLAGS) $(INCLUDE) $(TEST_INCLUDE) $(ASAN_CJELLYLIBRARY) $(TESTFLAGS)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(FUZZ_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(FUZZ_CC) $(FUZZ_CXX) $(FUZZ_SAN) $(FUZZ_LIB_FLAGS) $(FUZZ_BIN_FLAGS) $(INCLUDE) $(MODEL_LIBS) $(CUTIL_LIBS)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

####################################################################
# Flags stamp check
####################################################################
#
# An object records nothing about the flags it was built with. If the rule
# that builds it does not depend on something that changes when those flags
# change, a flag change rebuilds nothing, and the stale object links into
# everything downstream while looking exactly like a correct incremental
# build. The stamps above are that something. This checks that every rule
# needing one names one, and names the right one.
#
# It reads the makefile *text* rather than asking make for its rule database,
# because a rule inside a false `ifeq` is not in the database at all, and an
# audit that cannot see a branch reports full coverage of the branches it
# can. The `src/%.cpp` rule is that case here: cjelly has no C++ under src/
# today, so nothing in the build reaches it, and it is still a rule waiting
# for the first file that matches it.
#
# Three failures are possible and the gate separates them, because they want
# different fixes:
#
#   BAD         a rule names no stamp, or names another tree's. A rule copied
#               between trees keeps the old stamp and then misses exactly the
#               changes it was there to catch. The tree's name is the prefix
#               of both $(<TREE>_OBJ_DIR) and $(<TREE>_FLAGS_STAMP), so the
#               pairing is derived, and a fourth tree needs no edit here.
#   UNRECORDED  a rule names the right stamp but expands a variable the
#               stamp's own printf does not mention. A stamp only moves when
#               the text it prints changes, so a flag living only in an
#               omitted variable still rebuilds nothing. Naming a stamp is
#               half of the job; the other half is invisible.
#   UNMODELLED  a compiler invocation this gate does not model. Pinned, so a
#               new one has to be looked at rather than passing in silence.
#
# Recipe lines are joined across backslash continuations before their
# variables are read. Without that, a wrapped recipe hides every variable
# past the break - cjelly's fuzz harness link wraps, and $(MODEL_LIBS) and
# $(CUTIL_LIBS) live on its second line. The control plants that case.
define stamp-check-awk
{ L[NR] = $$0 }
function joinrec(i,   r, k) {
  r = L[i]; k = i
  while (k < NR && L[k] ~ /\\[ 	]*$$/) { k++; r = r " " L[k] }
  RECEND = k
  return r
}
function vars(s, out,   v) {
  while (match(s, /\$$\([A-Za-z0-9_]+\)/)) {
    v = substr(s, RSTART + 2, RLENGTH - 3)
    out[v] = 1
    s = substr(s, RSTART + RLENGTH)
  }
}
BEGIN {
  PREREQ_N = split("LIBOBJECTS ASAN_LIBOBJECTS FUZZ_OBJECTS", pa, " ")
  for (x = 1; x <= PREREQ_N; x++) PREREQ[pa[x]] = 1
}
END {
  total = 0; bad = 0; unmodelled = 0; unrecorded = 0; linked = 0
  for (i = 1; i <= NR; i++) {
    if (L[i] !~ /^\$$\([A-Z_]*FLAGS_STAMP\):/) continue
    name = L[i]; sub(/^\$$\(/, "", name); sub(/\).*/, "", name)
    for (j = i + 1; j <= NR && j < i + 8; j++) {
      if (L[j] !~ /printf/) continue
      delete tmp; vars(joinrec(j), tmp)
      for (v in tmp) if (v != "") SV[name "|" v] = 1
      break
    }
  }
  cur = ""; curline = 0; skipto = 0
  for (i = 1; i <= NR; i++) {
    if (i <= skipto) continue
    if (L[i] !~ /^\t/) {
      if (L[i] ~ /:/ && L[i] !~ /:=/ && L[i] !~ /^\043/ && L[i] !~ /^[ ]/) {
        cur = L[i]; curline = i; m = i
        while (m < NR && L[m] ~ /\\[ \t]*$$/) { m++; cur = cur " " L[m] }
        skipto = m
      }
      continue
    }
# Which population a recipe is in is decided from the JOINED record, not the
# first physical line: a compile rule that wraps before `-c $$<` otherwise
# reads as a link rule, and the wrong-tree check below lives only in the
# compile arm, so an object rule stamped for another tree passes in silence.
#
# Joining is only allowed to decide that. It must not become the unit that
# gets counted, which is the obvious next step and is wrong here: check-
# aliasing is one logical line holding three separate compiler invocations,
# and counting records instead of invocations takes UNMODELLED from 3 to 1
# while the pin, the message and the makefile all still say 3. So the link
# arm still walks physical lines, and only the compile arm claims its
# record.
#
# The stamped-link arm consumes its record, so LINKED counts RULES there,
# which is what the gate's own message says it counts. That is deliberate and
# not the collapse above: a record never spans two rules, so a second command
# inside one stamped link recipe is the same rule, under the same stamp, with
# its variables already read from the whole record. Nothing is lost.
#
# Do NOT "fix" this into counting invocations here. ghoti-io-3d's chron did
# count invocations while still reading each one's whole record, so a stamped
# link rule holding two commands reported the same unrecorded name twice, at
# two different line numbers - and UNRECORDED is pinned at zero, so the
# inflated number is the one a reader judges the damage by. The two units
# have to agree per arm: count invocations where the COUNT is the answer
# (unmodelled, which is a count of compiler calls outside the model), count
# rules where the STAMP is the answer. Measured here with a planted
# two-command link rule: one report, LINKED up by one.
    iscompile = (L[i] ~ /-c \$$</)
    if (!iscompile) {
      if (L[i] ~ /^\t[ \t]*\043/) continue
      if (L[i] ~ /-c \$$\$$</) continue
      head = L[i]
      sub(/^\t[ \t]*/, "", head)
      sub(/^[-@]+[ \t]*/, "", head)
      sub(/^if[ \t]+/, "", head)
# A negated invocation is still an invocation, and `if ! $$(CC) ...` is how a
# gate asks whether something FAILS to compile - so the shapes this sweep
# most needs to see are exactly the ones carrying a `!`. check-headers walked
# into this on the day it was added: two $$(CC) -E calls, neither counted,
# UNMODELLED still reading 3 and the gate still green.
      sub(/^![ \t]*/, "", head)
      sub(/^[-@]+[ \t]*/, "", head)
      if (head !~ /^\$$\$$?\([A-Z_]*(CC|CXX)\)[ \t]/ &&
          head !~ /^(cc|c\+\+|gcc|g\+\+|clang|clang\+\+)[ \t]/) continue
      if (joinrec(i) ~ /-c \$$</) iscompile = 1
    }
    if (!iscompile) {
      hdr = cur; j = curline
      if (hdr !~ /FLAGS_STAMP/) { unmodelled++; continue }
      linked++
      match(hdr, /\$$\([A-Z_]*FLAGS_STAMP\)/)
      sn = substr(hdr, RSTART + 2, RLENGTH - 3)
      rec = joinrec(i); skipto = RECEND
      delete rv; vars(rec, rv)
      for (v in rv) {
        if (v == "" || v in PREREQ) continue
        if (!((sn "|" v) in SV)) {
          unrecorded++
          printf "  %s:%d: link recipe expands $$(%s), which %s does not record\n", FILENAME, i, v, sn
        }
      }
      continue
    }
    rec = joinrec(i); skipto = RECEND
    total++
    hdr = cur; j = curline
    if (hdr !~ /FLAGS_STAMP/) {
      bad++
      printf "  %s:%d: compiles with no flags stamp: %s\n", FILENAME, j, hdr
      continue
    }
    tgt = hdr; sub(/:.*/, "", tgt)
    if (tgt ~ /OBJ_DIR/) {
      tree = tgt; sub(/.*\$$\(/, "", tree); sub(/OBJ_DIR.*/, "", tree)
      want = "$$(" tree "FLAGS_STAMP)"
      if (index(hdr, want) == 0) {
        bad++
        printf "  %s:%d: stamped for another tree, wants %s: %s\n", FILENAME, j, want, hdr
        continue
      }
    }
    match(hdr, /\$$\([A-Z_]*FLAGS_STAMP\)/)
    sn = substr(hdr, RSTART + 2, RLENGTH - 3)
    delete rv; vars(rec, rv)
    for (v in rv) {
      if (v == "" || v in PREREQ) continue
      if (!((sn "|" v) in SV)) {
        unrecorded++
        printf "  %s:%d: recipe expands $$(%s), which %s does not record\n", FILENAME, i, v, sn
      }
    }
  }
  printf "TOTAL %d BAD %d UNMODELLED %d UNRECORDED %d LINKED %d PREREQ %d\n", total, bad, unmodelled, unrecorded, linked, PREREQ_N
}
endef

STAMP_CHECK_MAKEFILE := $(firstword $(MAKEFILE_LIST))

# What this gate does not model, pinned so the set cannot grow in silence.
# Three today, all of them check-aliasing's: it compiles a generated control
# with -fsyntax-only, twice more in its failure branch to work out why. Those
# produce no artifact and run from scratch every time, so there is nothing
# for a stamp to keep current. Every other compiler invocation in this file
# is a compile or link rule that names its own tree's stamp.
#
# The pin earned itself immediately: it was 0, and adding check-aliasing in
# the next commit failed this gate rather than passing in silence.
STAMP_UNMODELLED_EXPECTED := 5

# Variable names the link sweep skips because the rule already lists them as
# file prerequisites, where mtime is the real check. Pinned so the list
# cannot grow into an excuse. $(CJELLYLIBRARY) is deliberately not in it: it
# carries $(IMAGE_LIBS) $(MODEL_LIBS) $(CUTIL_LIBS) as well as the archive
# path, and those are flags that no file's mtime covers.
STAMP_LINK_PREREQ_EXPECTED := 3

check-stamps: ## Fail if a compile or link rule has no flags stamp, or the wrong one
	@mkdir -p $(BUILD_DIR)
	$(file >$(BUILD_DIR)/stamp_check.awk,$(stamp-check-awk))
# An arm has to keep failing after the sweep is restructured, and one here did
# not: when the wrapped compile rule was the wrong-tree one, the compile arm
# exits before reading variables, so nothing in the control ever exercised
# joining on the compile side. Breaking it left the control green. There are
# now two wrapped compile rules - one wrong-tree, which arms the marker, and
# one correct-tree carrying an unrecorded variable on EACH side of the break,
# which arms the join twice over: lose the marker join and the rule is filed
# as a link, lose the variable join and only the name before the break is
# seen. An arm that has stopped failing reads exactly like an arm that passes.
#
# planted_link_ok is spelled c++ on purpose. The head pattern learned bare
# cc/c++ names, and every planted COMPILE rule is found by its -c $$< marker
# without ever reaching that pattern, so the addition was undemonstrable
# until one planted LINK rule used one.
#
# The control runs first, and is a planted set rather than a single bad rule:
# a sweep that has stopped matching recipes reports nothing wrong, which is
# indistinguishable from a clean makefile. So require it to find the planted
# faults and only those. The wrapped link rule is the arm for continuation
# joining; without that, the rule reads as clean, which is how this class of
# checker has already been wrong in two other libraries.
	@printf '%s\n\t%s\n\t\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n\t\t%s\n%s\n\t%s\n\t\t%s\n%s\n\t%s\n\t\t%s\n' \
		'$$(FLAGS_STAMP): force-flags' \
		"@printf '%s' \\\\" \
		"'\$$(CFLAGS) \$$(INCLUDE) \$$(LDFLAGS)' > \$$@.new" \
		'$$(OBJ_DIR)/%.o: src/%.c $$(FLAGS_STAMP)' \
		'cc $$(CFLAGS) $$(INCLUDE) -c $$< -o $$@' \
		'$$(OBJ_DIR)/planted_nostamp.o: src/planted.c' \
		'cc $$(CFLAGS) $$(INCLUDE) -c $$< -o $$@' \
		'$$(OBJ_DIR)/planted_unrecorded.o: src/planted2.c $$(FLAGS_STAMP)' \
		'cc $$(CFLAGS) $$(PLANTED_UNRECORDED) $$(INCLUDE) -c $$< -o $$@' \
		'$$(APP_DIR)/planted_link_ok: planted.o $$(FLAGS_STAMP)' \
		'c++ $$(LDFLAGS) -o $$@ planted.o' \
		'$$(APP_DIR)/planted_link_nostamp: planted.o' \
		'g++ $$(LDFLAGS) -o $$@ planted.o' \
		'$$(APP_DIR)/planted_link_unrec: planted.o $$(FLAGS_STAMP)' \
		'g++ $$(LDFLAGS) $$(PLANTED_LINK_UNRECORDED) -o $$@ planted.o' \
		'$$(APP_DIR)/planted_link_wrapped: planted.o $$(FLAGS_STAMP)' \
		'g++ $$(LDFLAGS) \\' \
		'$$(PLANTED_WRAPPED) -o $$@ planted.o' \
		'$$(ASAN_OBJ_DIR)/planted_wrapped_compile.o: src/p3.c $$(FLAGS_STAMP)' \
		'$$(CC) $$(CFLAGS) \\' \
		'$$(INCLUDE) -c $$< -o $$@' \
		'$$(OBJ_DIR)/planted_wrapped_vars.o: src/p4.c $$(FLAGS_STAMP)' \
		'cc $$(CFLAGS) $$(PLANTED_BEFORE_BREAK) \\' \
		'$$(PLANTED_PAST_BREAK) $$(INCLUDE) -c $$< -o $$@' \
		> $(BUILD_DIR)/stamp_control.mk
# PREREQ is stripped from the control's line rather than pinned in it. The
# same sweep produces both numbers, so a control that checked PREREQ too
# would fail first on any edit to the skip list and the arm below could never
# be seen to fire - coverage that cannot be demonstrated is not coverage.
	@ctl=$$(awk -f $(BUILD_DIR)/stamp_check.awk \
			$(BUILD_DIR)/stamp_control.mk | tail -1 | sed 's/ PREREQ [0-9]*$$//'); \
	want="TOTAL 5 BAD 2 UNMODELLED 1 UNRECORDED 5 LINKED 3"; \
	if [ "$$ctl" != "$$want" ]; then \
		printf "\033[0;31mcheck-stamps: the control says '%s', not '%s' - the sweep is not reading rules the way it thinks it is, so a clean result from it means nothing.\033[0m\n" "$$ctl" "$$want" >&2; \
		exit 1; \
	fi
# Two independent counts of the same population. If the sweep silently stops
# matching, its total falls away from grep's and the gate fails rather than
# passing on an empty sweep. Comment lines are dropped first, because the
# prose above names the marker it looks for and would be counted as a compile
# recipe itself.
	@want=$$(grep -v '^#' $(STAMP_CHECK_MAKEFILE) | grep -cF -- '-c $$<'); \
	out=$$(awk -f $(BUILD_DIR)/stamp_check.awk $(STAMP_CHECK_MAKEFILE)); \
	got=$$(printf '%s\n' "$$out" | sed -n 's/^TOTAL \([0-9]*\) .*/\1/p'); \
	bad=$$(printf '%s\n' "$$out" | sed -n 's/^TOTAL [0-9]* BAD \([0-9]*\) .*/\1/p'); \
	unmodelled=$$(printf '%s\n' "$$out" | sed -n 's/.* UNMODELLED \([0-9]*\) .*/\1/p'); \
	unrecorded=$$(printf '%s\n' "$$out" | sed -n 's/.* UNRECORDED \([0-9]*\) .*/\1/p'); \
	linked=$$(printf '%s\n' "$$out" | sed -n 's/.* LINKED \([0-9]*\) .*/\1/p'); \
	prereq=$$(printf '%s\n' "$$out" | sed -n 's/.* PREREQ \([0-9]*\)$$/\1/p'); \
	if [ "$$got" != "$$want" ]; then \
		printf "\033[0;31mcheck-stamps: the sweep saw %s compile recipes and grep found %s. One of them is wrong, so neither count can be trusted.\033[0m\n" "$$got" "$$want" >&2; \
		exit 1; \
	fi; \
	if [ "$$unmodelled" != "$(STAMP_UNMODELLED_EXPECTED)" ]; then \
		printf "\033[0;31mcheck-stamps: %s compiler invocations are outside what this gate models, not the %s it is pinned to. A rule that compiles or links without naming a stamp goes stale on its own unless something it depends on is stamped, and this gate does not check that - so the change needs a look.\033[0m\n" \
			"$$unmodelled" "$(STAMP_UNMODELLED_EXPECTED)" >&2; \
		exit 1; \
	fi; \
	if [ "$$prereq" != "$(STAMP_LINK_PREREQ_EXPECTED)" ]; then \
		printf "\033[0;31mcheck-stamps: the link sweep ignores %s variable names, not the %s it is pinned to. A name is skipped only because the rule already lists it as a file prerequisite, so mtime covers it; a name added for any other reason silences the check for that variable.\033[0m\n" \
			"$$prereq" "$(STAMP_LINK_PREREQ_EXPECTED)" >&2; \
		exit 1; \
	fi; \
	if [ "$$unrecorded" != "0" ]; then \
		printf "\033[0;31m\n### %s recipes expand a variable their stamp does not record ###\033[0m\n" "$$unrecorded" >&2; \
		printf '%s\n' "$$out" | grep 'does not record' >&2; \
		printf "\nNaming the right stamp is not enough: a stamp only moves when the\n" >&2; \
		printf "variables inside its own printf change. A flag that lives only in a\n" >&2; \
		printf "variable the stamp omits rebuilds nothing at all.\n" >&2; \
		exit 1; \
	fi; \
	if [ "$$bad" != "0" ]; then \
		printf "\033[0;31m\n### %s rules carry the wrong flags stamp, or none ###\033[0m\n" "$$bad" >&2; \
		printf '%s\n' "$$out" | grep -v '^TOTAL ' >&2; \
		printf "\nAn object built without its tree's stamp as a prerequisite is never\n" >&2; \
		printf "rebuilt when the flags change, and it links into everything\n" >&2; \
		printf "downstream of it.\n" >&2; \
		exit 1; \
	fi; \
	printf "\033[0;32mAll %s compile rules carry the flags stamp for their own tree and %s link rules carry theirs, every variable either recorded or a file prerequisite; %s compiler invocations are outside the model, as pinned.\033[0m\n" "$$got" "$$linked" "$$unmodelled"

####################################################################
# Strict aliasing check
####################################################################
#
# No sanitizer detects a strict-aliasing violation at any -O level: the
# instrumented binary runs the miscompiled code and exits 0. The compile-time
# warning is the only instrument there is, and it is silent unless
# -fstrict-aliasing is in effect, which gcc turns on from -O2.
#
# A real violation fails the build, because $(ALIASING_CFLAGS) sits under
# -Werror. So no sweep is needed for the findings - only for the instrument.
# A disarmed warning fails nothing and is indistinguishable from a clean
# library, which is the whole problem. This compiles a planted violation with
# the library's OWN flags and fails if it is accepted.
#
# THE SHAPE OF THE CONTROL IS LOAD-BEARING. Measured here at -O2, count of
# the diagnostic by level:
#
#                                          L0  L1  L2  L3
#   *(int *)f   where f is a PARAMETER      0   1   0   0
#   *(int *)&local, a known object          0   1   1   1
#   punning through a void *                0   0   0   0
#
# A control for a gate at level N must be caught at N and MISSED at N+1. One
# that survives into the weaker level still passes after the gate has fallen
# back to it, which is indistinguishable from working. This control is a
# parameter cast - caught at 1, missed at 2 - so it certifies level 1 and
# nothing else. The "simpler" spelling, punning a local whose address is
# taken, fires from level 1 upward and so would certify nothing at all.
#
# The last row is the standing limit: no level catches punning through a
# void *, so a clean build is not evidence about that class.
#
# WHY THE FAILURE BRANCH RE-COMPILES INSTEAD OF READING FLAGS. A warning that
# is switched off and a compiler that does not implement it look identical
# from the outside and want opposite fixes - repair the makefile, or stop
# believing this build has aliasing coverage. clang is the live case: it
# accepts -fstrict-aliasing -Wstrict-aliasing=1 in silence and implements no
# such diagnostic, so `make CC=clang` prints the flags on every compile line
# and detects nothing. So the branch asks the same compiler two more
# questions, empirically, rather than parsing what it was told.
check-aliasing: ## Fail if the strict-aliasing warning is no longer armed
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' \
		'#include <stdint.h>' \
		'int32_t cj_alias_control(float * f);' \
		'int32_t cj_alias_control(float * f) {' \
		'  return *(int32_t *)f;' \
		'}' > $(BUILD_DIR)/alias_control.c
	@if $(CC) $(LIB_CFLAGS) $(INCLUDE) -fsyntax-only \
			$(BUILD_DIR)/alias_control.c 2> $(BUILD_DIR)/alias_control.log; then \
		cc_name=$$($(CC) --version 2>/dev/null | head -1); \
		$(CC) $(LIB_CFLAGS) -O2 $(INCLUDE) -fsyntax-only \
			$(BUILD_DIR)/alias_control.c 2> $(BUILD_DIR)/alias_o2.log; \
		o2=$$?; \
		$(CC) $(LIB_CFLAGS) -O2 -Wstrict-aliasing=1 $(INCLUDE) -fsyntax-only \
			$(BUILD_DIR)/alias_control.c 2> $(BUILD_DIR)/alias_lvl.log; \
		lvl=$$?; \
		if [ "$$o2" != "0" ] && grep -q 'strict-aliasing' $(BUILD_DIR)/alias_o2.log; then \
			printf "\033[0;33mcheck-aliasing: %s accepted the planted violation, and the same flags plus -O2 diagnose it - so the warning is armed and the optimisation is not.\033[0m\n" "$$cc_name"; \
			printf '%s\n' \
				'  gcc enables -fstrict-aliasing from -O2 and the warning is silent without it, so' \
				'  this is what BUILD=debug looks like, where $$(OPT_CFLAGS) is -O0. Not a defect:' \
				'  the assumption is off, so there is nothing to miscompile. This build has no' \
				'  aliasing coverage either, which is why it is said out loud rather than passed' \
				'  in silence.'; \
			exit 0; \
		fi; \
		printf "\033[0;31mcheck-aliasing: %s accepted a planted type-punning violation.\033[0m\n" "$$cc_name" >&2; \
		if [ "$$lvl" != "0" ] && grep -q 'strict-aliasing' $(BUILD_DIR)/alias_lvl.log; then \
			printf '%s\n' \
				'  Adding -Wstrict-aliasing=1 explicitly does diagnose it, so the compiler has the' \
				'  warning and the library flags are not asking for it. A later explicit level is' \
				'  what overrides an earlier one, so this is $$(ALIASING_CFLAGS) removed or emptied,' \
				'  or a level passed in EXTRA_CFLAGS, which ends the line. Not a reordering against' \
				'  -Wall: its implied 3 loses to an explicit level from either side.' >&2; \
			exit 1; \
		else \
			printf '%s\n' \
				'  Adding -Wstrict-aliasing=1 explicitly changes nothing, so this compiler does not' \
				'  implement the warning - the flags are not the problem. Expect clang, which accepts' \
				'  -fstrict-aliasing -Wstrict-aliasing=1 in silence and diagnoses nothing under any' \
				'  spelling, -Weverything included. cjelly aliasing coverage is gcc-only, and this' \
				'  build does not have it.' >&2; \
			exit 1; \
		fi; \
	elif ! grep -q 'strict-aliasing' $(BUILD_DIR)/alias_control.log; then \
		printf "\033[0;31mcheck-aliasing: the control failed to compile, but not for aliasing - so this says nothing about whether the warning is armed:\033[0m\n" >&2; \
		cat $(BUILD_DIR)/alias_control.log >&2; \
		exit 1; \
	fi; \
	printf "\033[0;32mA planted type-punning violation is still refused by the library's own flags.\033[0m\n"

####################################################################
# Installed-header platform check
####################################################################

# Every header under include/ is installed, `*_internal.h` included, so a
# header that includes <X11/Xlib.h> or <windows.h> puts the whole window
# system's namespace into every consumer that includes it. application.h did
# exactly that, which is roughly 1,300 names on Linux and, on Windows, the
# macros that make `near`, `far` and `Rectangle` unusable as identifiers.
# cj_platform.h documents the opposite intent in its own first paragraph -
# "opaque, no platform headers required" - so this is the gate that makes
# the documented promise true of the rest of the API.
#
# The instrument is the PREPROCESSOR, not the compiler. Two states have to be
# told apart: a header that pulls X11 in, and a header that does not compile
# standalone at all. `cc -fsyntax-only` fails on both and they read alike -
# window_internal.h and rgraph_model_internal.h fail it today for missing a
# vulkan.h of their own, which has nothing to do with this question. `cc -E`
# type-checks nothing, so it answers only what was included.
#
# Pinned so that a sweep which stops finding headers fails instead of passing
# on an empty loop. Raise it when a header is added.
CHECK_HEADERS_EXPECTED := 33

check-headers: ## Fail if an installed header pulls the window system in with it
	@mkdir -p $(BUILD_DIR)
# The control runs first. A sweep that has stopped matching prints exactly
# what a clean tree prints, so require it to find a planted inclusion before
# any clean result from it is believed.
	@printf '#include <X11/Xlib.h>\n' > $(BUILD_DIR)/hdr_control.h
	@printf '#include "hdr_control.h"\n' > $(BUILD_DIR)/hdr_probe.c
	@if ! $(CC) -E $(CFLAGS) $(INCLUDE) -I $(BUILD_DIR) \
			$(BUILD_DIR)/hdr_probe.c 2>/dev/null | grep -q '/X11/'; then \
		printf "\033[0;31mcheck-headers: the control header includes X11/Xlib.h and the sweep did not see it, so a clean result from it means nothing.\033[0m\n" >&2; \
		exit 1; \
	fi
	@n=0; bad=0; \
	for h in $$(cd include && find . -name '*.h' ! -name 'platform_internal.h' \
			| sed 's|^\./||' | sort); do \
		n=$$((n+1)); \
		printf '#include <%s>\n' "$$h" > $(BUILD_DIR)/hdr_probe.c; \
		if ! $(CC) -E $(CFLAGS) $(INCLUDE) $(BUILD_DIR)/hdr_probe.c \
				> $(BUILD_DIR)/hdr_probe.i 2>/dev/null; then \
			printf "  %s: does not preprocess - an include it names is missing\n" "$$h" >&2; \
			bad=$$((bad+1)); continue; \
		fi; \
		if grep -qE '"[^"]*/X11/|"[^"]*[/\\]windows\.h"' $(BUILD_DIR)/hdr_probe.i; then \
			printf "  %s: pulls the window system in with it\n" "$$h" >&2; \
			bad=$$((bad+1)); \
		fi; \
	done; \
	if [ "$$n" != "$(CHECK_HEADERS_EXPECTED)" ]; then \
		printf "\033[0;31mcheck-headers: swept %s headers, not the %s it is pinned to. Either a header was added and the pin wants raising, or the sweep stopped finding them - and an empty sweep reports clean.\033[0m\n" "$$n" "$(CHECK_HEADERS_EXPECTED)" >&2; \
		exit 1; \
	fi; \
	if [ "$$bad" != "0" ]; then \
		printf "\033[0;31m\n### %s installed headers carry the window system with them ###\033[0m\n" "$$bad" >&2; \
		printf "\nAn implementation file that needs a native type includes\n" >&2; \
		printf "<ghoti.io/cjelly/platform_internal.h> first instead; it is the one header\n" >&2; \
		printf "this gate skips, and the only one allowed to name a window system.\n" >&2; \
		exit 1; \
	fi; \
	printf "\033[0;32mAll %s installed headers keep the window system to themselves.\033[0m\n" "$$n"

####################################################################
# check-quiet
####################################################################
#
# A library that prints has decided something on its embedder's behalf. The
# decision belongs in src/log.c, which is the one file allowed to name a
# stream; everything else says what happened and at what level, and the
# embedder picks a level and a sink.
#
# src/main.c is not library code - it is the demo, an application talking to
# the person who ran it - so it writes to stdout like any other program.
#
# THE INSTRUMENT IS A TEXT SWEEP, and it is worth being plain about what that
# can and cannot see:
#
#  - It cannot tell a call from the same word inside a comment or a string.
#    That direction is a false positive, which is loud and self-explaining;
#    the reverse - missing a real call - is what would matter, and text
#    cannot miss one, because a call has to be spelled out to compile.
#  - It reads the Win32 module too, which no compiler on this machine does.
#    That is not a bonus so much as the only check those files get.
#
# nm on the objects would be the sharper tool for "does this call printf",
# and it answers a different question: gcc rewrites fprintf(stderr, ...) to
# fwrite, and fwrite to a FILE* the caller opened is perfectly fine. The
# symbol cannot say which stream it meant. Text can.
#
# Pinned so that a sweep which stops finding sources fails instead of
# reporting a clean tree. Raise it when a source file is added.
CHECK_QUIET_EXPECTED := 19

# The library sources, less the two that are allowed to write to a stream.
CHECK_QUIET_SOURCES = $(shell find src -type f -name '*.c' \
	! -name 'main.c' ! -name 'log.c' | sort)

# Spelled once and used by both the control and the sweep, so that a pattern
# which stops matching cannot do so for the sweep alone.
CHECK_QUIET_PATTERN := (^|[^_[:alnum:]])(printf|fprintf|vfprintf|vprintf|puts|fputs|fputc|putchar|perror)[[:space:]]*\(|(^|[^_[:alnum:]])(stdout|stderr)([^_[:alnum:]]|$$)

check-quiet: ## Fail if a library source writes to stdout or stderr itself
	@mkdir -p $(BUILD_DIR)
# The control runs first. grep here is ugrep, whose regex dialect is not
# GNU's, so a pattern that has quietly stopped matching would let this gate
# print the same thing a clean tree prints. Plant each spelling and require
# the sweep to find it.
	@printf 'void c(void);\nvoid c(void) { fprintf(stderr, "x"); printf("y"); }\n' \
		> $(BUILD_DIR)/quiet_control.c
	@found=$$(grep -cE '$(CHECK_QUIET_PATTERN)' $(BUILD_DIR)/quiet_control.c || true); \
	if [ "$$found" = "0" ]; then \
		printf "\033[0;31mcheck-quiet: the control file writes to both streams and the sweep did not see it, so a clean result from it means nothing.\033[0m\n" >&2; \
		exit 1; \
	fi
	@n=0; bad=0; \
	for f in $(CHECK_QUIET_SOURCES); do \
		n=$$((n+1)); \
		hits=$$(grep -nE '$(CHECK_QUIET_PATTERN)' "$$f" || true); \
		if [ -n "$$hits" ]; then \
			bad=$$((bad+1)); \
			printf "  %s:\n" "$$f" >&2; \
			printf "%s\n" "$$hits" | sed 's/^/    /' >&2; \
		fi; \
	done; \
	if [ "$$n" != "$(CHECK_QUIET_EXPECTED)" ]; then \
		printf "\033[0;31mcheck-quiet: swept %s sources, not the %s it is pinned to. Either a source was added and the pin wants raising, or the sweep stopped finding them - and an empty sweep reports clean.\033[0m\n" "$$n" "$(CHECK_QUIET_EXPECTED)" >&2; \
		exit 1; \
	fi; \
	if [ "$$bad" != "0" ]; then \
		printf "\033[0;31m\n### %s library sources write to a stream themselves ###\033[0m\n" "$$bad" >&2; \
		printf "\nSay what happened and at what level instead:\n" >&2; \
		printf "  CJ_ERRORF / CJ_WARNF / CJ_INFOF / CJ_DEBUGF / CJ_TRACEF\n" >&2; \
		printf "from <ghoti.io/cjelly/cj_log.h>. The embedder chooses a level and a\n" >&2; \
		printf "sink; src/log.c is the one file that turns that into a write.\n" >&2; \
		exit 1; \
	fi; \
	printf "\033[0;32mAll %s library sources leave the writing to the log.\033[0m\n" "$$n"
