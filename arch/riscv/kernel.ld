/*
 * Minux 内核链接脚本
 *
 * OpenSBI 占据 0x80000000 起的一段内存，完成初始化后
 * 会跳转到 0x80200000，所以内核从这里开始存放。
 */
OUTPUT_ARCH(riscv)
ENTRY(_start_phys)

KERNEL_LOAD_PA = 0x80200000;
KERNEL_VA_OFFSET = 0xffffffc000000000; /* SV39_HIGH_START */

PHDRS
{
    text   PT_LOAD FLAGS(5);     /* R-X */
    rodata PT_LOAD FLAGS(4);     /* R-- */
    data   PT_LOAD FLAGS(6);     /* RW- */
}

SECTIONS
{
    . = KERNEL_VA_OFFSET + KERNEL_LOAD_PA;
    _kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_VA_OFFSET) {
        *(.text.entry)          /* _start 必须放在最前面 */
        *(.text .text.*)
    } :text
    _start_phys = LOADADDR(.text) + (_start - ADDR(.text));

    . = ALIGN(4096);
    _kernel_rodata = .;
    .rodata : AT(ADDR(.rodata) - KERNEL_VA_OFFSET) {
        *(.rodata .rodata.* .srodata .srodata.*)
    } :rodata

    . = ALIGN(4096);
    _kernel_data = .;
    .data : AT(ADDR(.data) - KERNEL_VA_OFFSET) {
        *(.data .data.* .sdata .sdata.*)
    } :data

    /* 启动栈放在 __bss_start 之前：清零 BSS 时不会把正在用的栈抹掉 */
    .bss : AT(ADDR(.bss) - KERNEL_VA_OFFSET) ALIGN(4096) {
        *(.bss.stack)
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        . = ALIGN(8);
        __bss_end = .;
    } :data

    . = ALIGN(4096);
    _kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame) *(.comment) *(.note .note.*)
    }
}
