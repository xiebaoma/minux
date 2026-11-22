    .section .rodata
    .align 2
    .globl user_probe_start, user_probe_end
user_probe_start:
    /* 栈顶预留的 32 字节依次放 seed、request 和 reply。 */
    addi    sp, sp, -32
    ld      s0, 0(sp)
    mv      gp, s0
    not     tp, s0
    mv      t3, sp
    li      sp, 0               /* 抢占时用户 sp 故意指向未映射地址。 */
    li      t1, 0
.Lpoll:
    bnez    sp, .Lbad_sp
    ld      t0, 8(t3)
    beq     t0, t1, .Lpoll
    mv      t1, t0
    addi    s0, s0, 1
    not     t2, tp
    add     t2, t2, gp
    add     t2, t2, s0
    sd      t2, 16(t3)
    j       .Lpoll
.Lbad_sp:
    j       .Lbad_sp
user_probe_end:
