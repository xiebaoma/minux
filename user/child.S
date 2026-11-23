#include <asm/unistd.h>

    .text
    .globl _start
_start:
    /* 从新栈取 argc、argv、envp，检查文件数据和跨页 BSS。 */
    andi t0, sp, 15
    bnez t0, .Lfail
    ld t0, 0(sp)
    li t1, 2
    bne t0, t1, .Lfail
    ld t0, 16(sp)
    lbu t1, 0(t0)
    li t2, '4'
    bne t1, t2, .Lfail
    lbu t1, 1(t0)
    li t2, '2'
    bne t1, t2, .Lfail
    ld t0, 24(sp)
    bnez t0, .Lfail
    ld t0, 32(sp)
    lbu t1, 0(t0)
    li t2, 'M'
    bne t1, t2, .Lfail
    ld t0, 40(sp)
    bnez t0, .Lfail
    la t0, scratch
    lw t1, 0(t0)
    bnez t1, .Lfail
    li t2, 4096
    add t0, t0, t2
    lw t1, 0(t0)
    bnez t1, .Lfail
    la t2, exit_code
    lw a0, 0(t2)
    sw a0, 0(t0)
    j .Lexit
.Lfail:
    li a0, 1
.Lexit:
    li a7, __NR_exit
    ecall

    .data
exit_code: .word 42
    .bss
    .balign 4
scratch: .skip 4100
