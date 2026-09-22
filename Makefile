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
	BUILD := linux/$(BUILD)

else ifeq ($(UNAME_S), Darwin)
	OS_NAME := Mac
	LIB_EXTENSION := dylib
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-install_name,$(BASE_NAME_PREFIX).dylib
	TARGET := $(BASE_NAME_PREFIX).dylib
	EXE_EXTENSION :=
	# Additional macOS-specific variables
	BUILD := mac/$(BUILD)

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
	BUILD := win32/$(BUILD)

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
	BUILD := win64/$(BUILD)

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


CXX := g++
CXXFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wfatal-errors -std=c++20 -O1 -g $(EXTRA_CXXFLAGS)
CC := cc
CFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wfatal-errors -std=c17 -O0 -g `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags vulkan` $(EXTRA_CFLAGS)
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
DEPLESS_GOALS := clean docs docs-pdf cloc help uninstall uninstall-debug
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
SOURCES := $(shell find src -type f -name '*.c' ! -name 'main.c')

# Convert each source file path to an object file path.
LIBOBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SOURCES))


TESTFLAGS := `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs --cflags gtest`

# The checks `make test` runs besides the tests themselves. Named in a
# variable so that a build which cannot satisfy them can clear it: the
# coverage target does, because --coverage links the gcov runtime, whose
# mangle_path check-symbols is right to reject in a shipping library and
# wrong to reject in an instrumented one. Spelled as text's TEST_GATES is.
TEST_GATES ?= check-symbols



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

define unit-test-rule
$(APP_DIR)/$2$(EXE_EXTENSION): $1 $(APP_DIR)/$(STATIC_TARGET) | $(APP_DIR)/$(TARGET)
	@printf "\n### Compiling and linking %s Test ###\n" "$2"
	@mkdir -p $$(@D)
	$$(CXX) $$(CXXFLAGS) $$(TEST_INCLUDE) -MMD -MP -MF $$(APP_DIR)/$2.d -o $$@ $$< $$(CJELLYLIBRARY) $$(LDFLAGS) $$(TESTFLAGS)
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

$(OBJ_DIR)/%.o: src/%.c | $(LIBVER_GEN)
	@printf "\n### Compiling $@ ###\n"
	@mkdir -p $(@D)
	$(CC) $(LIB_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

# Pattern rule for C++ source files (if any):
$(OBJ_DIR)/%.o: src/%.cpp
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

$(APP_DIR)/$(TARGET): \
		$(LIBOBJECTS)
	@printf "\n### Compiling CJelly Library ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^ $(LDFLAGS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)

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
		$(APP_DIR)/$(TARGET)
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
.PHONY: all demo install test test-watch uninstall watch check-symbols
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

check-symbols: ## Fail if any exported symbol lacks the version namespace
check-symbols: $(APP_DIR)/$(TARGET)
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
	@badguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0} !d && /^#ifndef/{print $$2; d=1}' {} + \
		| awk '$$1 !~ /^GHOTI_IO_CJ_/ {print $$1}' || true); \
	if [ -n "$$badguards" ]; then \
		printf "\033[0;31m\n### Include guards with the wrong prefix ###\033[0m\n" >&2; \
		printf "%s\n" "$$badguards" >&2; \
		printf "\nGuards mirror the path: GHOTI_IO_CJ_<PATH>_H. A guard without the\n" >&2; \
		printf "library token is one rename away from colliding with another library's.\n" >&2; \
		exit 1; \
	fi
	@dupguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0} !d && /^#ifndef/{print $$2; d=1}' {} + \
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
ASAN_UBSAN_FLAGS := -fsanitize=address,$(UBSAN_CHECKS) \
	-fno-sanitize-recover=$(UBSAN_CHECKS) -fno-omit-frame-pointer -g

ASAN_BUILD_DIR := ./build/$(BUILD)-asan
ASAN_OBJ_DIR := $(ASAN_BUILD_DIR)/objects
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

$(ASAN_OBJ_DIR)/%.o: src/%.c | $(LIBVER_GEN)
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
$(ASAN_APP_DIR)/$2$(EXE_EXTENSION): $1 $(ASAN_APP_DIR)/$(ASAN_STATIC_TARGET)
	@printf "\n### Compiling and linking ASan %s Test ###\n" "$2"
	@mkdir -p $$(@D)
	$$(CXX) $$(ASAN_CXXFLAGS) $$(TEST_INCLUDE) -MMD -MP -MF $$(ASAN_APP_DIR)/$2.d -o $$@ $$< $$(ASAN_CJELLYLIBRARY) $$(ASAN_LDFLAGS) $$(TESTFLAGS)
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

$(FUZZ_OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	@$(FUZZ_CC) $(FUZZ_LIB_FLAGS) -std=c17 -w -DCJELLY_BUILD $(INCLUDE) -c $< -o $@

# $1 = harness basename (fuzz_mesh), $2 = target suffix (mesh)
define fuzz-rule
fuzz-$2: ## Build the $2 fuzz harness (requires clang)
fuzz-$2: $$(FUZZ_APP_DIR)/$1

$$(FUZZ_APP_DIR)/$1: tests/fuzz/$1.cpp tests/fuzz/fuzz_mesh_invariants.h $$(FUZZ_OBJECTS)
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

