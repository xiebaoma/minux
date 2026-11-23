#include <asm/unistd.h>
#include <minux/errno.h>
#include <minux/signal.h>
#include <minux/syscall.h>
#include "user_wait_probe.h"

    .macro probe_action action
    li      a0, \action
    li      a7, __NR_test_wait
    ecall
    .endm

    .macro wait4 pid, status, opts=0
    mv      a0, \pid
    mv      a1, \status
    li      a2, \opts
    li      a3, 0
    li      a7, __NR_wait4
    ecall
    .endm

    .section .rodata
    .align 2
    .globl wait_probe_start, wait_probe_end
    .globl wait_probe_parent_first, wait_probe_child_first
    .globl wait_probe_parent_fault, wait_probe_parent_orphans
    .globl wait_probe_fork, wait_probe_fork_oom, wait_probe_exec_errors
    .globl wait_probe_exec
    .globl wait_probe_child, wait_probe_fault
wait_probe_start:
wait_probe_parent_first:
    addi    sp, sp, -16
    li      t0, -1
    sd      t0, 0(sp)
    probe_action WAIT_PROBE_SPAWN
    blez    a0, .Lfail
    mv      s0, a0
    li      s1, -1

    wait4   s1, sp, 2           /* 尚未实现的选项。 */
    li      t0, -EINVAL
    bne     a0, t0, .Lfail
    addi    t1, s0, 1
    wait4   t1, sp              /* 有活子进程，但是 PID 不匹配。 */
    li      t0, -ECHILD
    bne     a0, t0, .Lfail
    wait4   s1, sp, WNOHANG
    bnez    a0, .Lfail
    ld      t0, 0(sp)
    bne     t0, s1, .Lfail      /* 无退出状态时不写用户缓冲区。 */

    wait4   s0, sp
    bne     a0, s0, .Lfail
    lw      t0, 0(sp)
    li      t1, WAIT_PROBE_EXIT_STATUS
    bne     t0, t1, .Lfail
    lw      t0, 4(sp)
    bne     t0, s1, .Lfail      /* wait4 只写 4 字节状态。 */
    wait4   s1, zero
    li      t0, -ECHILD
    bne     a0, t0, .Lfail
    j       .Ldone

wait_probe_child_first:
    addi    sp, sp, -16
    li      s1, -1
    probe_action WAIT_PROBE_SPAWN_EXITED
    blez    a0, .Lfail
    mv      s0, a0
    wait4   s1, sp, WNOHANG
    bne     a0, s0, .Lfail
    lw      t0, 0(sp)
    li      t1, WAIT_PROBE_EXIT_STATUS
    bne     t0, t1, .Lfail

    probe_action WAIT_PROBE_SPAWN_EXITED
    blez    a0, .Lfail
    mv      s0, a0
    li      t1, 1               /* 空页保留，状态地址无效。 */
    wait4   s0, t1
    li      t0, -EFAULT
    bne     a0, t0, .Lfail
    wait4   s0, zero            /* 写回失败时子进程也已回收。 */
    li      t0, -ECHILD
    bne     a0, t0, .Lfail

    probe_action WAIT_PROBE_SPAWN_EXITED
    blez    a0, .Lfail
    mv      s0, a0
    wait4   s0, zero
    bne     a0, s0, .Lfail
    j       .Ldone

wait_probe_parent_fault:
    addi    sp, sp, -16
    probe_action WAIT_PROBE_SPAWN_FAULT
    blez    a0, .Lfail
    mv      s0, a0
    wait4   s0, sp
    bne     a0, s0, .Lfail
    lw      t0, 0(sp)
    li      t1, SIGTRAP
    bne     t0, t1, .Lfail
    j       .Ldone

wait_probe_exec:
    addi    sp, sp, -32
    li      a0, SIGCHLD
    li      a1, 0
    li      a7, __NR_clone
    ecall
    bltz    a0, .Lfail
    beqz    a0, .Lexec_init
    mv      s0, a0
    wait4   s0, sp
    bne     a0, s0, .Lfail
    lw      t0, 0(sp)
    bnez    t0, .Lfail
    j       .Ldone
.Lexec_init:
    la      a0, .Linit_path
    sd      a0, 0(sp)
    sd      zero, 8(sp)
    mv      a1, sp
    li      a2, 0
    li      a7, __NR_execve
    ecall
    j       .Lfail

wait_probe_exec_errors:
    addi    sp, sp, -288
    li      s1, 0x1234
    sd      s1, 0(sp)
    li      a7, __NR_execve
    la      a0, .Ltruncated
    li      a1, 0
    li      a2, 0
    ecall
    li      t0, -ENOEXEC
    bne     a0, t0, .Lfail
    la      a0, .Lmissing
    ecall
    li      t0, -ENOENT
    bne     a0, t0, .Lfail
    li      a0, 1
    ecall
    li      t0, -EFAULT
    bne     a0, t0, .Lfail
    la      s0, .Lchild_path
    mv      a0, s0
    li      a2, 1
    ecall
    li      t0, -EFAULT
    bne     a0, t0, .Lfail
    mv      a0, s0
    li      a2, 0
    addi    a1, sp, 8
    li      t0, 1
    sd      t0, 0(a1)          /* 无效 argv[0]。 */
    ecall
    li      t0, -EFAULT
    bne     a0, t0, .Lfail
    mv      t0, a1
    li      t1, 33
.Lfill_argv:
    sd      s0, 0(t0)
    addi    t0, t0, 8
    addi    t1, t1, -1
    bnez    t1, .Lfill_argv
    sd      zero, 0(t0)
    mv      a0, s0
    ecall
    li      t0, -E2BIG
    bne     a0, t0, .Lfail
    ld      t0, 0(sp)
    bne     t0, s1, .Lfail
    j       .Ldone

wait_probe_fork_oom:
    li      a7, __NR_clone
    li      a0, SIGCHLD
    li      a1, 0
    ecall
    li      t0, -ENOMEM
    bne     a0, t0, .Lfail
    addi    sp, sp, -16
    sd      t0, 0(sp)
    li      s0, -1
    wait4   s0, zero, WNOHANG
    li      t0, -ECHILD
    bne     a0, t0, .Lfail
    la      a0, .Lchild_path
    li      a1, 0
    li      a2, 0
    li      a7, __NR_execve
    ecall
    ld      t0, 0(sp)          /* 内存不足返回原入口之后，旧栈仍可读写。 */
    bne     a0, t0, .Lfail
    sd      zero, 0(sp)
    j       .Ldone

wait_probe_fork:
    addi    sp, sp, -32
    li      a7, __NR_getpid
    ecall
    mv      s1, a0
    li      s2, FORK_PROBE_ROUNDS

    li      a7, __NR_clone
    li      a0, SIGCHLD
    mv      a1, sp              /* 自定义子栈超出当前 fork 子集。 */
    ecall
    li      t0, -EINVAL
    bne     a0, t0, .Lfail
    li      a0, 0x111           /* CLONE_VM | SIGCHLD */
    li      a1, 0
    ecall
    li      t0, -EINVAL
    bne     a0, t0, .Lfail
.Lfork_round:
    sd      s2, 0(sp)
    li      s3, 0x1234
    sd      s3, 8(sp)
    li      a0, SIGCHLD
    li      a1, 0
    li      a7, __NR_clone
    ecall
    beqz    a0, .Lfork_child
    bltz    a0, .Lfail
    mv      s0, a0
    beq     s0, s1, .Lfail
    andi    t0, s2, 1
    beqz    t0, .Luser_store
    mv      a0, sp              /* copy_to_user 是 fork 后对共享栈的第一次写。 */
    addi    a1, sp, 8
    li      a2, 8
    li      a7, __NR_test_copy
    ecall
    bnez    a0, .Lfail
    j       .Lfork_wait
.Luser_store:
    sd      s3, 0(sp)           /* 父进程直接写，期间没有切换页表。 */
.Lfork_wait:
    addi    s4, sp, 16
    wait4   s0, s4
    bne     a0, s0, .Lfail
    andi    t0, s0, 63
    ori     t0, t0, 64
    slli    t0, t0, 8
    lw      t1, 0(s4)
    bne     t0, t1, .Lfail
    ld      t0, 0(sp)
    bne     t0, s3, .Lfail
    addi    s2, s2, -1
    bnez    s2, .Lfork_round
    j       .Ldone
.Lfork_child:
    ld      t0, 0(sp)
    bne     t0, s2, .Lfail      /* 仍读到 fork 时的栈内容。 */
    sd      zero, 0(sp)
    li      a7, __NR_getpid
    ecall
    beq     a0, s1, .Lfail
    andi    a0, a0, 63
    ori     a0, a0, 64
    j       .Lexit

wait_probe_parent_orphans:
    probe_action WAIT_PROBE_SPAWN_EXITED
    blez    a0, .Lfail
    probe_action WAIT_PROBE_SPAWN
    blez    a0, .Lfail
    /* 父进程退出时同时有僵尸子进程和仍可运行的子进程。 */
.Ldone:
    probe_action WAIT_PROBE_PARENT_DONE
    li      a0, 0
    j       .Lexit
.Lfail:
    li      a0, 1
    j       .Lexit

wait_probe_child:
    probe_action WAIT_PROBE_CHILD_STARTED
    li      a0, WAIT_PROBE_EXIT_CODE
.Lexit:
    li      a7, __NR_exit
    ecall

wait_probe_fault:
    ebreak
    j       wait_probe_fault
.Ltruncated: .asciz "/test/truncated"
.Lmissing: .asciz "/no-such-file"
.Linit_path: .asciz "/init"
.Lchild_path: .asciz "/bin/child"
wait_probe_end:
