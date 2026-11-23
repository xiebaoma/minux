    .section .rodata
    .balign 8
    .global init_elf, init_elf_end
init_elf:
    .incbin INIT_ELF
init_elf_end:
    .balign 8
    .global child_elf, child_elf_end
child_elf:
    .incbin CHILD_ELF
child_elf_end:
