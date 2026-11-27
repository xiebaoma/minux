#include <asm/unistd.h>

    .text
    .globl __syscall6
    .type __syscall6, @function
__syscall6:
    /* C 调用将调用号与最多六个参数依次放在 a0 至 a6。 */
    mv      a7, a0
    mv      a0, a1
    mv      a1, a2
    mv      a2, a3
    mv      a3, a4
    mv      a4, a5
    mv      a5, a6
    ecall
    ret
    .size __syscall6, . - __syscall6

    .globl _exit
    .type _exit, @function
_exit:
    li      a7, __NR_exit
    ecall
1:  j       1b
    .size _exit, . - _exit
