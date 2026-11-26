#include <asm/unistd.h>
#include <minux/fcntl.h>
#include <minux/signal.h>

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

    .globl dup3
    .type dup3, @function
dup3:
    li      a7, __NR_dup3
    ecall
    ret
    .size dup3, . - dup3

    .globl pipe
    .type pipe, @function
pipe:
    li      a1, 0               /* pipe2(fds, 0) */
    li      a7, __NR_pipe2
    ecall
    ret
    .size pipe, . - pipe

    .globl write
    .type write, @function
write:
    /* C 调用已将 fd、buf、count 放入 a0、a1、a2。 */
    li      a7, __NR_write
    ecall
    ret
    .size write, . - write

    .globl fork
    .type fork, @function
fork:
    /* clone(SIGCHLD, NULL, NULL, 0, NULL) */
    li      a0, SIGCHLD
    li      a1, 0
    li      a2, 0
    li      a3, 0
    li      a4, 0
    li      a7, __NR_clone
    ecall
    ret
    .size fork, . - fork

    .globl execve
    .type execve, @function
execve:
    li      a7, __NR_execve
    ecall
    ret
    .size execve, . - execve

    .globl waitpid
    .type waitpid, @function
waitpid:
    li      a3, 0               /* wait4 的 rusage 为 NULL。 */
    li      a7, __NR_wait4
    ecall
    ret
    .size waitpid, . - waitpid

    .globl _exit
    .type _exit, @function
_exit:
    li      a7, __NR_exit
    ecall
    .size _exit, . - _exit
