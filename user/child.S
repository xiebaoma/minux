#include <asm/unistd.h>

    .text
    .globl _start
_start:
    /* 从新栈取 argc、argv、envp，检查文件数据和跨页 BSS。 */
    andi t0, sp, 15
    bnez t0, .Lfail
    ld t0, 0(sp)
    li t1, 2
    beq t0, t1, .Largs
    li t1, 3
    bne t0, t1, .Lfail
.Largs:
    slli t1, t0, 3
    add s0, sp, t1              /* argv 最后一个元素的位置。 */
    ld t0, 16(sp)
    lbu t1, 0(t0)
    li t2, '4'
    bne t1, t2, .Lfail
    lbu t1, 1(t0)
    li t2, '2'
    bne t1, t2, .Lfail
    ld t0, 8(s0)
    bnez t0, .Lfail
    ld t0, 16(s0)
    lbu t1, 0(t0)
    li t2, 'M'
    bne t1, t2, .Lfail
    ld t0, 24(s0)
    bnez t0, .Lfail
    la t0, scratch
    lw t1, 0(t0)
    bnez t1, .Lfail
    li t2, 4096
    add t0, t0, t2
    lw t1, 0(t0)
    bnez t1, .Lfail
    mv s1, t0
    ld t1, 24(sp)
    beqz t1, .Lsuccess          /* 两参数调用仍只验证装载与退出。 */
    li a0, 0
    li t3, 10
.Lfd_digit:
    lbu t2, 0(t1)
    addi t2, t2, -'0'
    bgeu t2, t3, .Lfail
    mul a0, a0, t3
    add a0, a0, t2
    addi t1, t1, 1
    lbu t2, 0(t1)
    bnez t2, .Lfd_digit
    /* exec 后沿用传入的 fd 和当前位置，退出时由内核归还引用。 */
    la a1, scratch
    li a2, 1
    li a7, __NR_read
    ecall
    li t1, 1
    bne a0, t1, .Lfail
    la t0, scratch
    lbu t1, 0(t0)
    li t2, 'b'
    bne t1, t2, .Lfail
.Lsuccess:
    la t2, exit_code
    lw a0, 0(t2)
    sw a0, 0(s1)
    j .Lexit
.Lfail:
    li a0, 1
.Lexit:
    li a7, __NR_exit
    ecall

    .data
#ifndef CHILD_EXIT_CODE
#define CHILD_EXIT_CODE 42
#endif
exit_code: .word CHILD_EXIT_CODE
    .bss
    .balign 4
scratch: .skip 4100
