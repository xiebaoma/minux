/*
 * 自测在打开中断前运行。临时接管 stvec，让一次访存异常返回
 * { scause, stval }，然后恢复正式的 trap 入口。
 */
    .text

    .macro begin_probe name
    .globl \name
\name:
    csrr    t0, stvec
    lla     t1, .Lrestore
    mv      t2, ra
    lla     t3, .Lprobe_trap
    csrw    stvec, t3
    .endm

    begin_probe sv39_probe_read
    lw      zero, 0(a0)
    j       .Lno_fault

    begin_probe sv39_probe_write
    lw      t3, 0(a0)
    sw      t3, 0(a0)
    j       .Lno_fault

    begin_probe sv39_probe_exec
    fence.i
    jalr    ra, 0(a0)

.Lno_fault:
    li      a0, 0
    li      a1, 0
.Lrestore:
    csrw    stvec, t0
    jr      t2

    .balign 4
.Lprobe_trap:
    csrr    a0, scause
    csrr    a1, stval
    csrw    sepc, t1
    sret
