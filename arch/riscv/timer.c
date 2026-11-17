#include <asm/sbi.h>
#include <asm/timer.h>
#include <minux/panic.h>

#define SIE_STIE (1UL << 5)

static u64 tick_interval;
static volatile unsigned long ticks;

static u64 read_time(void)
{
	u64 now;

	/* time 是 CPU 的单调递增计数器，单位由 DTB 指定。 */
	asm volatile("rdtime %0" : "=r"(now));
	return now;
}

void timer_init(u32 timebase_hz)
{
	if (timebase_hz < TIMER_HZ)
		panic("invalid timebase frequency: %u", timebase_hz);

	/* SBI TIME 需要绝对时间；这里每秒安排 TIMER_HZ 次中断。 */
	tick_interval = timebase_hz / TIMER_HZ;
	if (sbi_set_timer(read_time() + tick_interval))
		panic("failed to set first timer event");
	asm volatile("csrs sie, %0" : : "r"(SIE_STIE) : "memory");
}

void timer_handle_interrupt(void)
{
	/* 先安排下一次，避免返回后立即重复处理同一个待处理中断。 */
	if (sbi_set_timer(read_time() + tick_interval))
		panic("failed to rearm timer");
	ticks++;
}

unsigned long timer_ticks(void)
{
	/* 当前只运行一个 hart；中断处理器是唯一写入者。 */
	return ticks;
}
