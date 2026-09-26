/* SPDX-License-Identifier: MIT
 * Embeds the spelling dictionary into the binary so hed is a single
 * self-contained file. This is a portable C replacement for the GNU-assembler
 * src/dict_embed.S, which used directives (.type @object, .note.GNU-stack)
 * that the macOS assembler does not accept.
 *
 * The byte array is generated at build time from data/en_freq.txt by the
 * Makefile (see the dict_embed.inc rule) and #included here, so the same source
 * builds unchanged on Linux, macOS, and WSL.
 *
 * Dictionary: data/en_freq.txt = SymSpell frequency_dictionary_en_82_765.txt
 * (MIT, (c) 2018 Wolf Garbe). */
#include "dict_embed.inc"
