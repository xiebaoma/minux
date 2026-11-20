// platform-level interrupt controller

#include <asm/plic.h>
#include <asm/page.h>
#include <minux/panic.h>

#define PLIC_PRIORITY       0x000000UL
#define PLIC_ENABLE         0x002000UL
#define PLIC_ENABLE_STRIDE  0x000080UL
#define PLIC_CONTEXT       0x200000UL
#define PLIC_CONTEXT_STRIDE 0x001000UL
#define PLIC_THRESHOLD      0x0UL
#define PLIC_CLAIM_COMPLETE 0x4UL

static uintptr_t plic_base;
static size_t plic_size;
static u32 plic_context;
static u32 plic_ndev;

static u32 plic_read(u32 offset)
{
	return *(volatile u32 *)(plic_base + offset);
}

static void plic_write(u32 offset, u32 value)
{
	*(volatile u32 *)(plic_base + offset) = value;
}

static u32 context_offset(void)
{
	return PLIC_CONTEXT + plic_context * PLIC_CONTEXT_STRIDE;
}

void plic_init(const struct virt_irq_devices *devices)
{
	plic_base = (uintptr_t)arch_phys_to_virt(devices->plic_base);
	plic_size = devices->plic_size;
	plic_context = devices->plic_s_context;
	plic_ndev = devices->plic_ndev;

	/* 阈值 0 接受所有正优先级中断；具体来源由 enable 位控制。 */
	plic_write(context_offset() + PLIC_THRESHOLD, 0);
}

void plic_enable_irq(u32 irq)
{
	u32 enable_offset, value;

	if (!irq || irq > plic_ndev)
		panic("invalid PLIC IRQ: %u", irq);
	if (PLIC_PRIORITY + irq * 4UL + 4 > plic_size)
		panic("PLIC priority register outside MMIO range");
	enable_offset = PLIC_ENABLE + plic_context * PLIC_ENABLE_STRIDE +
			(irq / 32) * 4;
	if ((size_t)enable_offset + 4 > plic_size)
		panic("PLIC enable register outside MMIO range");

	/* 优先级 0 表示禁用；只打开指定设备的 S 模式 enable 位。 */
	plic_write(PLIC_PRIORITY + irq * 4, 1);
	value = plic_read(enable_offset);
	plic_write(enable_offset, value | (1U << (irq % 32)));
	asm volatile("fence iorw, iorw" : : : "memory");
}

u32 plic_claim(void)
{
	/* 读取 claim 寄存器会取得最高优先级的待处理中断编号。 */
	return plic_read(context_offset() + PLIC_CLAIM_COMPLETE);
}

void plic_complete(u32 irq)
{
	/* 先完成设备寄存器读写，再通知 PLIC 此中断已处理完。 */
	asm volatile("fence iorw, iorw" : : : "memory");
	plic_write(context_offset() + PLIC_CLAIM_COMPLETE, irq);
}
