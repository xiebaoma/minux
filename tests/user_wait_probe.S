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
wait_probe_end:
