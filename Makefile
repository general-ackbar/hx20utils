# HX-20 tools Makefile
# Build two utilities:
#  - hx20tape        : Encodes ASCII/TOKEN BASIC files to HX-20 WAV tape images
#  - hx20tokenizer   : Tokenizes/Detokenizes HX-20 BASIC files
#
# Usage:
#   make            # builds both binaries
#   make hx20tape   # builds only hx20tape
#   make hx20tokenizer
#   make install    # install to $(PREFIX)/bin (default /usr/local)
#   make clean
#
# Note on std::filesystem:
#   On very old GCC (<= 8), you may need to link with -lstdc++fs.
#   If you get unresolved filesystem symbols, build with:
#       make FS_LIB=-lstdc++fs
#
# Override variables on the command line if needed, e.g.:
#   make CXX=clang++ CXXFLAGS='-std=c++20 -O3 -march=native'
#
CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS   ?=
LDLIBS    ?= $(FS_LIB)

# Installation prefix
PREFIX    ?= /usr/local

# Sources
SOURCES   := hx20tape.cpp hx20tokenizer.cpp
BINARIES  := hx20tape hx20tokenizer

# Default target
all: $(BINARIES)

hx20tape: hx20tape.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

hx20tokenizer: hx20tokenizer.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

# Install binaries to $(PREFIX)/bin
install: $(BINARIES)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BINARIES) $(DESTDIR)$(PREFIX)/bin

# Remove build artifacts
clean:
	rm -f $(BINARIES)

.PHONY: all install clean
