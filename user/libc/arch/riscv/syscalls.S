#include <asm/unistd.h>

    .text
    .globl write
    .type write, @function
write:
    /* C 调用已将 fd、buf、count 放入 a0、a1、a2。 */
    li      a7, __NR_write
    ecall
    ret
    .size write, . - write

    .globl _exit
    .type _exit, @function
_exit:
    li      a7, __NR_exit
    ecall
    .size _exit, . - _exit
