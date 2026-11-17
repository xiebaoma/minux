#include <asm/trap.h>

/*
 * v0.2 第一阶段只处理 S 模式内核中的 trap，继续使用当前内核栈。
 * 将来进入用户态时，需要先切换到内核栈，不能直接使用用户 sp。
 */
    .section .text
    .align 2
    .globl riscv_trap_entry
riscv_trap_entry:
    /* 288 字节是 16 的倍数，调用 C 函数时保持 ABI 要求的栈对齐。 */
    addi    sp, sp, -PT_REGS_SIZE
    sd      zero,   0(sp)
    sd      ra,     8(sp)
    sd      t0,    40(sp)       /* 先保存 t0，才能用它计算旧 sp。 */
    addi    t0, sp, PT_REGS_SIZE
    sd      t0,    16(sp)
    sd      gp,    24(sp)
    sd      tp,    32(sp)
    sd      t1,    48(sp)
    sd      t2,    56(sp)
    sd      s0,    64(sp)
    sd      s1,    72(sp)
    sd      a0,    80(sp)
    sd      a1,    88(sp)
    sd      a2,    96(sp)
    sd      a3,   104(sp)
    sd      a4,   112(sp)
    sd      a5,   120(sp)
    sd      a6,   128(sp)
    sd      a7,   136(sp)
    sd      s2,   144(sp)
    sd      s3,   152(sp)
    sd      s4,   160(sp)
    sd      s5,   168(sp)
    sd      s6,   176(sp)
    sd      s7,   184(sp)
    sd      s8,   192(sp)
    sd      s9,   200(sp)
    sd      s10,  208(sp)
    sd      s11,  216(sp)
    sd      t3,   224(sp)
    sd      t4,   232(sp)
    sd      t5,   240(sp)
    sd      t6,   248(sp)

    /* 硬件保存返回地址和状态到 CSR；软件将它们放入统一的现场。 */
    csrr    t0, sepc
    sd      t0, PT_REGS_EPC_OFFSET(sp)
    csrr    t0, sstatus
    sd      t0, PT_REGS_STATUS_OFFSET(sp)
    csrr    t0, scause
    sd      t0, PT_REGS_CAUSE_OFFSET(sp)
    csrr    t0, stval
    sd      t0, PT_REGS_TVAL_OFFSET(sp)

    mv      a0, sp
    call    riscv_trap_handler

    /* 允许后续中断处理器修改 epc/status，再从现场恢复并执行 sret。 */
    ld      t0, PT_REGS_EPC_OFFSET(sp)
    csrw    sepc, t0
    ld      t0, PT_REGS_STATUS_OFFSET(sp)
    csrw    sstatus, t0
    ld      ra,     8(sp)
    ld      gp,    24(sp)
    ld      tp,    32(sp)
    ld      t1,    48(sp)
    ld      t2,    56(sp)
    ld      s0,    64(sp)
    ld      s1,    72(sp)
    ld      a0,    80(sp)
    ld      a1,    88(sp)
    ld      a2,    96(sp)
    ld      a3,   104(sp)
    ld      a4,   112(sp)
    ld      a5,   120(sp)
    ld      a6,   128(sp)
    ld      a7,   136(sp)
    ld      s2,   144(sp)
    ld      s3,   152(sp)
    ld      s4,   160(sp)
    ld      s5,   168(sp)
    ld      s6,   176(sp)
    ld      s7,   184(sp)
    ld      s8,   192(sp)
    ld      s9,   200(sp)
    ld      s10,  208(sp)
    ld      s11,  216(sp)
    ld      t3,   224(sp)
    ld      t4,   232(sp)
    ld      t5,   240(sp)
    ld      t6,   248(sp)
    ld      t0,    40(sp)
    ld      sp,    16(sp)       /* 最后恢复进入 trap 前的栈指针。 */
    sret
