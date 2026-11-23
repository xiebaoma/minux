#include <asm/unistd.h>
#include <minux/signal.h>

    .text
    .globl _start
_start:
    ld t0, 0(sp)                /* argc == 1，argv[0] 指向程序路径。 */
    li t1, 1
    bne t0, t1, .Lfail
    ld t0, 8(sp)
    lbu t0, 0(t0)
    li t1, '/'
    bne t0, t1, .Lfail
    li a0, SIGCHLD
    li a1, 0
    li a7, __NR_clone
    ecall
    bltz a0, .Lfail
    beqz a0, .Lchild
    mv s0, a0
    addi sp, sp, -16
    mv a1, sp
    li a2, 0
    li a3, 0
    li a7, __NR_wait4
    ecall
    bne a0, s0, .Lfail
    lw t0, 0(sp)
    li t1, (42 << 8)
    bne t0, t1, .Lfail
    la t0, child_argv
    ld t0, 8(t0)                /* 子进程填写 argv[1] 后，父进程仍应读到 NULL。 */
    bnez t0, .Lfail
    li a0, 0
    j .Lexit

.Lchild:
    la a0, child_path
    la a1, child_argv
    la t0, argument
    sd t0, 8(a1)                /* 写共享数据页触发 COW，execve 从私有副本读取参数。 */
    la a2, child_envp
    li a7, __NR_execve
    ecall
.Lfail:
    li a0, 1
.Lexit:
    li a7, __NR_exit
    ecall

    .section .rodata
child_path: .asciz "/bin/child"
argument:   .asciz "42"
environment: .asciz "MINUX=1"

    .data
    .balign 8
child_argv: .dword child_path, 0, 0
child_envp: .dword environment, 0
