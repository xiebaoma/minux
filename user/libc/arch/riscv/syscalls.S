#include <asm/unistd.h>
#include <minux/fcntl.h>

    .text
    .globl open
    .type open, @function
open:
    /* open(path, flags[, mode]) -> openat(AT_FDCWD, path, flags, mode) */
    li      a3, 0
    andi    t0, a1, O_CREAT
    beqz    t0, 1f
    mv      a3, a2
1:  mv      a2, a1
    mv      a1, a0
    li      a0, AT_FDCWD
    li      a7, __NR_openat
    ecall
    ret
    .size open, . - open

    .globl read
    .type read, @function
read:
    li      a7, __NR_read
    ecall
    ret
    .size read, . - read

    .globl close
    .type close, @function
close:
    li      a7, __NR_close
    ecall
    ret
    .size close, . - close

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
