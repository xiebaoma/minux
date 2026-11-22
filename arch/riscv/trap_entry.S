#include <asm/trap.h>

    .section .text
    .align 2
    .globl riscv_trap_entry
riscv_trap_entry:
    /* 用户态时 sscratch 指向内核栈；内核态时它为零。 */
    csrrw   sp, sscratch, sp
    bnez    sp, .Lfrom_user
    csrrw   sp, sscratch, sp
    addi    sp, sp, -PT_REGS_SIZE
    sd      t0,    40(sp)       /* 先保存 t0，才能用它计算旧 sp。 */
    addi    t0, sp, PT_REGS_SIZE
    j       .Lsave_regs
.Lfrom_user:
    addi    sp, sp, -PT_REGS_SIZE
    sd      t0,    40(sp)
    csrrw   t0, sscratch, zero  /* 取出用户 sp，恢复内核态约定。 */
.Lsave_regs:
    sd      zero,   0(sp)
    sd      ra,     8(sp)
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
    andi    t0, t0, SSTATUS_SPP
    bnez    t0, .Lsave_cause
    ld      gp, USER_FRAME_GP_OFFSET(sp)
    ld      tp, USER_FRAME_TP_OFFSET(sp)
.Lsave_cause:
    csrr    t0, scause
    sd      t0, PT_REGS_CAUSE_OFFSET(sp)
    csrr    t0, stval
    sd      t0, PT_REGS_TVAL_OFFSET(sp)

    mv      a0, sp
    call    riscv_trap_handler

.Ltrap_return:
    ld      t0, PT_REGS_STATUS_OFFSET(sp)
    andi    t1, t0, SSTATUS_SPP
    bnez    t1, .Lrestore_regs

    /* 返回 U 模式前保存内核 gp、tp，并准备下一次用户 trap 的栈。 */
    sd      gp, USER_FRAME_GP_OFFSET(sp)
    sd      tp, USER_FRAME_TP_OFFSET(sp)
    addi    t1, sp, PT_REGS_SIZE
    csrw    sscratch, t1
.Lrestore_regs:
    csrw    sstatus, t0
    ld      t0, PT_REGS_EPC_OFFSET(sp)
    csrw    sepc, t0
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

    .align 2
    .globl arch_enter_user
    .type arch_enter_user, @function
arch_enter_user:
    /* 新程序的指令已被写入用户页，从预建现场走统一返回路径。 */
    addi    sp, a0, -USER_FRAME_SIZE
    fence.i
    j       .Ltrap_return
    .size arch_enter_user, . - arch_enter_user
