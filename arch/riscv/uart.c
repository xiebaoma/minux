#include <asm/plic.h>
#include <asm/uart.h>

#define UART_RBR 0 /* 读：接收字符；写：发送字符 THR。 */
#define UART_THR 0
#define UART_IER 1
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5

#define UART_IER_RDI  0x01 /* 接收数据可用中断 */
#define UART_FCR_FIFO 0x01
#define UART_LCR_DLAB 0x80
#define UART_MCR_OUT2 0x08
#define UART_LSR_DR   0x01
#define UART_LSR_THRE 0x20
#define SIE_SEIE      (1UL << 9)

static uintptr_t uart_base;
static u32 uart_irq;

static u8 uart_read(u32 offset)
{
	return *(volatile u8 *)(uart_base + offset);
}

static void uart_write(u32 offset, u8 value)
{
	*(volatile u8 *)(uart_base + offset) = value;
}

static void uart_putc(u8 ch)
{
	/* 只发送少量回显字符；等发送寄存器空闲后再写入。 */
	while (!(uart_read(UART_LSR) & UART_LSR_THRE))
		;
	uart_write(UART_THR, ch);
}

void uart_init(const struct virt_irq_devices *devices)
{
	uart_base = devices->uart_base;
	uart_irq = devices->uart_irq;

	/* 保留 OpenSBI 设置的波特率，清 DLAB 后偏移 1 才是 IER。 */
	uart_write(UART_LCR, uart_read(UART_LCR) & ~UART_LCR_DLAB);
	uart_write(UART_IER, 0);
	/* 开 FIFO，接收阈值为 1 字节；保留启动期间已收到的字符。 */
	uart_write(UART_FCR, UART_FCR_FIFO);
	uart_write(UART_MCR, uart_read(UART_MCR) | UART_MCR_OUT2);
	plic_enable_irq(uart_irq);
	uart_write(UART_IER, UART_IER_RDI);
	asm volatile("fence iorw, iorw" : : : "memory");
	/* 设备和 PLIC 都已配置，最后才开放 S 模式外部中断。 */
	asm volatile("csrs sie, %0" : : "r"(SIE_SEIE) : "memory");
}

u32 uart_irq_number(void)
{
	return uart_irq;
}

void uart_handle_interrupt(void)
{
	/* 一次中断读空接收 FIFO，避免只读一个字符就反复进入 trap。 */
	while (uart_read(UART_LSR) & UART_LSR_DR) {
		u8 ch = uart_read(UART_RBR);

		if (ch == '\r')
			uart_putc('\r');
		uart_putc(ch == '\r' ? '\n' : ch);
	}
}
