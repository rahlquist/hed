# hed - SPDX-License-Identifier: MIT
CXX      ?= g++
CXXFLAGS ?= -O2 -g0
CXXFLAGS += -std=c++17 -Wall -Wextra -MMD -MP
LDFLAGS  ?=
PREFIX   ?= /usr/local
DESTDIR  ?=

# macOS: g++ is clang++ (Xcode CLT / Homebrew). -static is unsupported there.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
STATIC_LDFLAGS := -s
else
STATIC_LDFLAGS := -static -s
endif

SRC := src/main.cpp src/editor.cpp src/highlight.cpp src/spell.cpp src/util.cpp
OBJ := $(SRC:.cpp=.o) src/dict_embed.o

all: hed

hed: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS)

# Dictionary embedding: portable C wrapper. gen_dict.py turns data/en_freq.txt
# into a C string literal (dict_embed.inc), which dict_embed.c #includes. This
# replaces the old GNU-assembler src/dict_embed.S, whose directives (.type
# @object, .note.GNU-stack) the macOS assembler rejects. Works on Linux, macOS,
# and WSL alike.
src/dict_embed.inc: data/en_freq.txt src/gen_dict.py
	python3 src/gen_dict.py data/en_freq.txt $@

src/dict_embed.o: src/dict_embed.c src/dict_embed.inc
	$(CXX) -c -o $@ src/dict_embed.c

# fully static binary: copy it to any x86_64/arm64 Linux box, no deps
# (macOS does not support fully static binaries; -s strips instead)
static:
	$(MAKE) clean
	$(MAKE) LDFLAGS="$(STATIC_LDFLAGS)"

install: hed
	install -Dm755 hed $(DESTDIR)$(PREFIX)/bin/hed
	install -Dm644 completions/hed.bash $(DESTDIR)$(PREFIX)/share/bash-completion/completions/hed
	install -Dm644 completions/hed.fish $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/hed.fish
	install -Dm644 completions/hed.zsh $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_hed

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hed $(DESTDIR)$(PREFIX)/share/bash-completion/completions/hed \
	      $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/hed.fish \
	      $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_hed

test: hed
	bash tests/run.sh
	python3 tests/editor_pty.py

clean:
	rm -f hed src/*.o src/*.d src/dict_embed.inc

-include $(OBJ:.o=.d)
.PHONY: all static install uninstall test clean
