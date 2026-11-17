/* 单 hart 的 S 模式周期性时钟。 */
#ifndef _ASM_RISCV_TIMER_H
#define _ASM_RISCV_TIMER_H

#include <minux/types.h>

#define TIMER_HZ 10U

void timer_init(u32 timebase_hz);
void timer_handle_interrupt(void);
unsigned long timer_ticks(void);

#endif /* _ASM_RISCV_TIMER_H */
