SUITE := ghoti.io
PROJECT := cjelly

BUILD ?= release
BRANCH := -dev
# If BUILD is debug, append -debug
ifeq ($(BUILD),debug)
    BRANCH := $(BRANCH)-debug
endif

BASE_NAME := lib$(SUITE)-$(PROJECT)$(BRANCH).so
BASE_NAME_PREFIX := lib$(SUITE)-$(PROJECT)$(BRANCH)
MAJOR_VERSION := 0
MINOR_VERSION := 0.0
SO_NAME := $(BASE_NAME).$(MAJOR_VERSION)
ENV_VARS :=

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


CXX := g++
CXXFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c++20 -O1 -g
CC := cc
CFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c17 -O0 -g `PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags vulkan`
# Library-specific compile flags (export symbols on Windows, PIC on Linux)
LIB_CFLAGS := $(CFLAGS) -DCJELLY_BUILD
# -DGHOTIIO_CUTIL_ENABLE_MEMORY_DEBUG
LDFLAGS := -L /usr/lib -lstdc++ -lm `PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs --cflags vulkan`
BUILD_DIR := ./build/$(BUILD)
OBJ_DIR := $(BUILD_DIR)/objects
GEN_DIR := $(BUILD_DIR)/generated
APP_DIR := $(BUILD_DIR)/apps


# Add OS-specific flags
ifeq ($(UNAME_S), Linux)
	CFLAGS += `PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags x11`
	LDFLAGS += `PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs x11`
	LIB_CFLAGS += -fPIC

else ifeq ($(UNAME_S), Darwin)

else ifeq ($(findstring MINGW32_NT,$(UNAME_S)),MINGW32_NT)  # 32-bit Windows
	LDFLAGS += -lgdi32

else ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)  # 64-bit Windows
	LDFLAGS += -lgdi32

else
	$(error Unsupported OS: $(UNAME_S))

endif

# The standard include directories for the project.
INCLUDE := -I include/ -I $(GEN_DIR)/

# Automatically collect all .c source files under the src directory.
SOURCES := $(shell find src -type f -name '*.c')

# Convert each source file path to an object file path.
LIBOBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SOURCES))


TESTFLAGS := `PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs --cflags gtest`


CJELLYLIBRARY := -L $(APP_DIR) -l$(SUITE)-$(PROJECT)$(BRANCH)


all: $(APP_DIR)/$(TARGET) $(APP_DIR)/main$(EXE_EXTENSION) ## Build the shared library

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

# Automatically include all generated dependency files.
-include $(wildcard $(OBJ_DIR)/*.d)


####################################################################
# Object Files
####################################################################

# Pattern rule for C source files: compile .c files to .o files, generating dependency files.
$(OBJ_DIR)/%.o: src/%.c
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

# Phony target to ensure all shader headers are generated before any object files
.PHONY: shader-headers
shader-headers: $(SHADER_HEADERS)

# Make specific object files that need shader headers depend on them
$(OBJ_DIR)/rgraph.o: shader-headers

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
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(LDFLAGS) $(CJELLYLIBRARY)

####################################################################
# Commands
####################################################################

# General commands
.PHONY: clean cloc docs docs-pdf
# Release build commands
.PHONY: all install test test-watch uninstall watch
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

test: ## Make and run the Unit tests
test: \
		$(TEST_FILES) \
		$(APP_DIR)/$(TARGET) \
		$(APP_DIR)/main$(EXE_EXTENSION)
#				$(APP_DIR)/test$(EXE_EXTENSION) \

	@printf "\033[0;32m\n"
	@printf "############################\n"
	@printf "### Running normal tests ###\n"
	@printf "############################\n"
	@printf "\033[0m\n"
	cd $(APP_DIR) && LD_LIBRARY_PATH="./" $(ENV_VARS) ./main$(EXE_EXTENSION)

clean: ## Remove all contents of the build directories.
	-@rm -rvf $(BUILD_DIR)

# Files will be as follows:
# /usr/local/lib/(SUITE)/
#   lib(SUITE)-(PROJECT)(BRANCH).so.(MAJOR).(MINOR)
#   lib(SUITE)-(PROJECT)(BRANCH).so.(MAJOR) link to previous
#   lib(SUITE)-(PROJECT)(BRANCH).so link to previous
# /etc/ld.so.conf.d/(SUITE)-(PROJECT)(BRANCH).conf will point to /usr/local/lib/(SUITE)
# /usr/local/include/(SUITE)/(PROJECT)(BRANCH)
#   *.h copied from ./include/(PROJECT)
# /usr/local/share/pkgconfig
#   (SUITE)-(PROJECT)(BRANCH).pc created

install: ## Install the library globally, requires sudo
	# Installing the shared library.
	@mkdir -p $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
# Install the .so file
	@cp $(APP_DIR)/$(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/
	@ln -f -s $(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)
	# Installing the ld configuration file.
	@echo "/usr/local/lib/$(SUITE)" > /etc/ld.so.conf.d/$(SUITE)-$(PROJECT)$(BRANCH).conf
endif
ifeq ($(OS_NAME), Windows)
# The .dll file and the .dll.a file
	@mkdir -p $(BIN_INSTALL_PATH)/$(SUITE)
	@cp $(APP_DIR)/$(TARGET).a $(LIB_INSTALL_PATH)
	@cp $(APP_DIR)/$(TARGET) $(BIN_INSTALL_PATH)
endif
	# Installing the headers.
	@mkdir -p $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/$(PROJECT)
	@cp include/cjelly/*.h $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/$(PROJECT)
	@cp $(GEN_DIR)/*.h $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/$(PROJECT)
	# Installing the pkg-config files.
	@mkdir -p $(PKG_CONFIG_PATH)
	@cat pkgconfig/$(SUITE)-$(PROJECT).pc | sed 's/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g; s/(VERSION)/$(VERSION)/g; s|(LIB)|$(LIB_INSTALL_PATH)|g; s|(INCLUDE)|$(INCLUDE_INSTALL_PATH)|g' > $(PKG_CONFIG_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
ifeq ($(OS_NAME), Linux)
	# Running ldconfig.
	@ldconfig >> /dev/null 2>&1
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) installed"

uninstall: ## Delete the globally-installed files.  Requires sudo.
	# Deleting the shared library.
ifeq ($(OS_NAME), Linux)
	@rm -f $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)*
	# Deleting the ld configuration file.
	@rm -f /etc/ld.so.conf.d/$(SUITE)-$(PROJECT)$(BRANCH).conf
endif
ifeq ($(OS_NAME), Windows)
	@rm -f $(LIB_INSTALL_PATH)/$(TARGET).a
	@rm -f $(BIN_INSTALL_PATH)/$(TARGET)
endif
	# Deleting the headers.
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	# Deleting the pkg-config files.
	@rm -f $(PKG_CONFIG_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
	# Cleaning up (potentially) no longer needed directories.
	@rmdir --ignore-fail-on-non-empty $(INCLUDE_INSTALL_PATH)/$(SUITE)
	@rmdir --ignore-fail-on-non-empty $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
	# Running ldconfig.
	@ldconfig >> /dev/null 2>&1
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

help: ## Display this help
	@grep -E '^[ a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "%-15s %s\n", $$1, $$2}' | sed "s/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g"

valgrind: all ## Run main under valgrind with suppressions
	cd $(APP_DIR) && LD_LIBRARY_PATH=./ valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --suppressions=$(abspath tools/valgrind.supp) ./main$(EXE_EXTENSION)

