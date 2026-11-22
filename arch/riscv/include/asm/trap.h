/* RISC-V trap 入口与保存现场的布局。 */
#ifndef _ASM_RISCV_TRAP_H
#define _ASM_RISCV_TRAP_H

/* x0~x31 各占 8 字节，随后保存 trap 相关的四个 CSR。 */
#define PT_REGS_EPC_OFFSET     256
#define PT_REGS_STATUS_OFFSET  264
#define PT_REGS_CAUSE_OFFSET   272
#define PT_REGS_TVAL_OFFSET    280
#define PT_REGS_SIZE           288

/* 用户 trap 现场上方还有一段空间，用于保留内核 gp 和 tp。 */
#define USER_FRAME_GP_OFFSET   PT_REGS_SIZE
#define USER_FRAME_TP_OFFSET   (PT_REGS_SIZE + 8)
#define USER_FRAME_SIZE        (PT_REGS_SIZE + 16)

#define SSTATUS_SIE            (1 << 1)
#define SSTATUS_SPIE           (1 << 5)
#define SSTATUS_SPP            (1 << 8)

#ifndef __ASSEMBLER__

#include <minux/stddef.h>

struct pt_regs {
	unsigned long x[32]; /* x0 固定为 0；x2 保存进入 trap 前的 sp。 */
	unsigned long epc;
	unsigned long status;
	unsigned long cause;
	unsigned long tval;
};

/* 汇编按固定偏移写入，布局变化必须在编译时被发现。 */
_Static_assert(offsetof(struct pt_regs, epc) == PT_REGS_EPC_OFFSET,
	       "pt_regs epc offset mismatch");
_Static_assert(offsetof(struct pt_regs, status) == PT_REGS_STATUS_OFFSET,
	       "pt_regs status offset mismatch");
_Static_assert(offsetof(struct pt_regs, cause) == PT_REGS_CAUSE_OFFSET,
	       "pt_regs cause offset mismatch");
_Static_assert(offsetof(struct pt_regs, tval) == PT_REGS_TVAL_OFFSET,
	       "pt_regs tval offset mismatch");
_Static_assert(sizeof(struct pt_regs) == PT_REGS_SIZE,
	       "pt_regs size mismatch");

void trap_init(void);

#endif /* !__ASSEMBLER__ */
#endif /* _ASM_RISCV_TRAP_H */
