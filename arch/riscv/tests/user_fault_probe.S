#include <asm/unistd.h>
#include <minux/errno.h>
#include "user_fault_probe.h"

    .macro expect_efault
    li      a7, __NR_test_copy
    ecall
    li      t0, -EFAULT
    bne     a0, t0, .Lstop
    .endm

    .section .rodata
    .align 2
    .globl fault_probe_start, fault_probe_end
    .globl fault_probe_gap, fault_probe_readonly, fault_probe_noexec, fault_probe_valid
fault_probe_start:
fault_probe_gap:
    li      t0, PROBE_ANON_END
    lbu     t1, 0(t0)
    j       .Lreport

fault_probe_readonly:
    li      t0, PROBE_RO
    lbu     t1, 0(t0)            /* 先让只读页建立映射。 */
    sb      zero, 0(t0)
    j       .Lreport

fault_probe_noexec:
    li      t0, PROBE_ANON
    li      t1, 0x00008067       /* ret */
    sw      t1, 0(t0)
    fence.i
    jalr    t0
    j       .Lreport

fault_probe_valid:
    li      s0, PROBE_ANON
    lbu     t0, 0(s0)            /* 首次读应重试并得到零。 */
    bnez    t0, .Lstop
    li      s1, PROBE_ANON + PROBE_PAGE_BYTES
    li      t0, 0x73
    sb      t0, 0(s1)            /* 另一页的首次访问是写。 */
    li      t0, 0x5a
    sb      t0, -1(s1)
    sb      t0, 0(s0)

    /* 源和目标都跨页，目标页此前均未映射。 */
    li      s2, PROBE_ANON + 3 * PROBE_PAGE_BYTES - 1
    mv      a0, s2
    addi    a1, s1, -1
    li      a2, 2
    li      a7, __NR_test_copy
    ecall
    bnez    a0, .Lstop
    lbu     t0, 0(s2)
    lbu     t1, 1(s2)
    li      t2, 0x5a
    bne     t0, t2, .Lstop
    li      t2, 0x73
    bne     t1, t2, .Lstop

    /* 从尚无映射的匿名页拷贝，也应得到零。 */
    mv      a0, s0
    li      a1, PROBE_ANON + 4 * PROBE_PAGE_BYTES
    li      a2, 1
    li      a7, __NR_test_copy
    ecall
    bnez    a0, .Lstop
    lbu     t0, 0(s0)
    bnez    t0, .Lstop

    /* 用户地址加长度会回绕。 */
    mv      a0, s0
    li      a1, -1
    li      a2, 2
    expect_efault

    /* 源区间只有第一个字节可读。 */
    mv      a0, s0
    li      a1, PROBE_ANON_END - 1
    li      a2, 2
    expect_efault

    /* 目标 VMA 只读，即使该页仍未建立映射也要拒绝。 */
    li      a0, PROBE_RO
    mv      a1, s0
    li      a2, 1
    expect_efault

.Lreport:
    addi    a0, sp, -1
    mv      a1, a0
    li      a2, 1
    li      a7, __NR_test_copy
    ecall
.Lstop:
    ebreak
    j       .Lstop
fault_probe_end:
