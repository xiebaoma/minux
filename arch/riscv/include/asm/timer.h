/* 单 hart 的 S 模式周期性时钟。 */
#ifndef _ASM_RISCV_TIMER_H
#define _ASM_RISCV_TIMER_H

#include <minux/timer.h>
#include <minux/types.h>

void timer_init(u32 timebase_hz);
void timer_handle_interrupt(void);

#endif /* _ASM_RISCV_TIMER_H */
