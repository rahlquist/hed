/* SPDX-License-Identifier: MIT
 * Embeds the spelling dictionary into the binary so hed is a single self-contained file.
 * Dictionary: data/en_freq.txt = SymSpell frequency_dictionary_en_82_765.txt (MIT, (c) 2018 Wolf Garbe). */
    .section .rodata
    .global hed_dict_data
    .type hed_dict_data, @object
    .balign 16
hed_dict_data:
    .incbin "data/en_freq.txt"
    .global hed_dict_end
    .type hed_dict_end, @object
hed_dict_end:
    .byte 0
    .section .note.GNU-stack,"",@progbits
