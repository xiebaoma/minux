/*
 * Minux 内核链接脚本
 *
 * OpenSBI 占据 0x80000000 起的一段内存，完成初始化后
 * 会跳转到 0x80200000，所以内核从这里开始存放。
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

BASE_ADDRESS = 0x80200000;

SECTIONS
{
    . = BASE_ADDRESS;
    _kernel_start = .;

    .text : {
        *(.text.entry)          /* _start 必须放在最前面 */
        *(.text .text.*)
    }

    .rodata : ALIGN(8) {
        *(.rodata .rodata.* .srodata .srodata.*)
    }

    .data : ALIGN(8) {
        *(.data .data.* .sdata .sdata.*)
    }

    /* 启动栈放在 __bss_start 之前：清零 BSS 时不会把正在用的栈抹掉 */
    .bss : ALIGN(8) {
        *(.bss.stack)
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        . = ALIGN(8);
        __bss_end = .;
    }

    _kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame) *(.comment) *(.note .note.*)
    }
}
