OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS {
    . = 0x10000;
    /* 由链接器按实际存在的节生成 LOAD 段，纯只读程序只需一个 RX 段。 */
    .text : { *(.text .text.*) *(.rodata .rodata.*) }
    . = ALIGN(4096);
    .data : { *(.data .data.* .sdata .sdata.*) }
    .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.*) }
}
