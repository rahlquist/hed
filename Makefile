# hed - SPDX-License-Identifier: MIT
CXX      ?= g++
CXXFLAGS ?= -O2 -g0
CXXFLAGS += -std=c++17 -Wall -Wextra -MMD -MP
LDFLAGS  ?=
PREFIX   ?= /usr/local
DESTDIR  ?=

SRC := src/main.cpp src/editor.cpp src/highlight.cpp src/spell.cpp src/util.cpp
OBJ := $(SRC:.cpp=.o) src/dict_embed.o

all: hed

hed: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS)

src/dict_embed.o: src/dict_embed.S data/en_freq.txt
	$(CXX) -c -o $@ src/dict_embed.S

# fully static binary: copy it to any x86_64/arm64 Linux box, no deps
static:
	$(MAKE) clean
	$(MAKE) LDFLAGS="-static -s"

install: hed
	install -Dm755 hed $(DESTDIR)$(PREFIX)/bin/hed
	install -Dm644 completions/hed.bash $(DESTDIR)$(PREFIX)/share/bash-completion/completions/hed
	install -Dm644 completions/hed.fish $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/hed.fish

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hed $(DESTDIR)$(PREFIX)/share/bash-completion/completions/hed \
	      $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/hed.fish

test: hed
	bash tests/run.sh
	python3 tests/editor_pty.py

clean:
	rm -f hed src/*.o src/*.d

-include $(OBJ:.o=.d)
.PHONY: all static install uninstall test clean
