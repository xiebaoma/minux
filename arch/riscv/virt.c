#include <asm/virt.h>
#include <minux/fdt.h>
#include <minux/limits.h>
#include <minux/panic.h>
#include <minux/string.h>

/* fdt_walk 的回调没有私有参数；启动时只有一个 hart 顺序执行探测。 */
static struct virt_irq_devices *probe;
static bool in_soc, in_plic, in_uart;
static bool soc_has_ranges, plic_compatible, uart_compatible;
static u32 soc_address_cells, soc_size_cells;
static u32 plic_phandle, uart_interrupt_parent;

/*
 这个函数是在设备树的 compatible 属性里，查找有没有某个指定的设备名称。

compatible 不是单个普通字符串，而可能是一串以 \0 分隔的字符串，例如：
"sifive,plic-1.0.0\0riscv,plic0\0"
参数分别是：`value` 指向这串数据，`len` 是总字节数，`wanted` 是要找的名字。循环每次处理其中一个字符串：
1. off 指向当前字符串开头；`end` 向后找它的 \0。
2. 如果到 len 还没找到 \0，数据格式有问题，返回 false，也避免读出边界。
3. 先比较长度，再用 memcmp 比较内容；完全相同就返回 true。
4. 没匹配就把 off 移到 \0 后面，检查下一个字符串。
全部找完仍没匹配，就返回 false。比如在上面的数据中查找 "riscv,plic0"，结果就是 true。
*/
static bool compatible_has(const void *value, u32 len, const char *wanted)
{
	const char *strings = value;
	size_t wanted_len = strlen(wanted);

	/* compatible 是一个或多个以 NUL 结尾的字符串。 */
	for (u32 off = 0; off < len;) {
		u32 end = off;

		while (end < len && strings[end] != '\0')
			end++;
		if (end == len)
			return false;
		if (end - off == wanted_len &&
		    memcmp(strings + off, wanted, wanted_len) == 0)
			return true;
		off = end + 1;
	}
	return false;
}

static void read_mmio_reg(const void *value, u32 len,
			  uintptr_t *base, size_t *size)
{
	u64 address, bytes;
	u32 address_len = soc_address_cells * 4;
	u32 size_len = soc_size_cells * 4;

	/* 当前 QEMU virt 的设备各只有一组物理地址与长度。 */
	if (soc_address_cells < 1 || soc_address_cells > 2 ||
	    soc_size_cells < 1 || soc_size_cells > 2 ||
	    len != address_len + size_len ||
	    !fdt_read_cells(value, address_len, soc_address_cells, &address) ||
	    !fdt_read_cells((const u8 *)value + address_len, size_len,
			    soc_size_cells, &bytes) ||
	    !bytes || address > ULONG_MAX || bytes > ULONG_MAX ||
	    address > ULONG_MAX - bytes)
		panic("invalid QEMU virt MMIO reg");
	*base = (uintptr_t)address;
	*size = (size_t)bytes;
}

static void probe_node(const char *name, unsigned int depth)
{
	if (depth == 1)
		in_soc = strcmp(name, "soc") == 0;
	if (depth == 2) {
		in_plic = in_soc &&
			strncmp(name, "interrupt-controller@", 21) == 0;
		in_uart = in_soc && strncmp(name, "serial@", 7) == 0;
	}
}

static void probe_property(const char *name, const void *value,
			   u32 len, unsigned int depth)
{
	if (depth == 1 && in_soc) {
		if (strcmp(name, "#address-cells") == 0) {
			if (!fdt_read_u32(value, len, &soc_address_cells))
				panic("invalid /soc/#address-cells");
		} else if (strcmp(name, "#size-cells") == 0) {
			if (!fdt_read_u32(value, len, &soc_size_cells))
				panic("invalid /soc/#size-cells");
		} else if (strcmp(name, "ranges") == 0) {
			/* 空 ranges 表示子设备地址与父总线地址相同。 */
			if (len)
				panic("unsupported /soc address translation");
			soc_has_ranges = true;
		}
		return;
	}
	if (depth != 2)
		return;
	if (in_plic) {
		if (strcmp(name, "compatible") == 0)
			plic_compatible = compatible_has(value, len,
						     "sifive,plic-1.0.0");
		else if (strcmp(name, "reg") == 0)
			read_mmio_reg(value, len, &probe->plic_base,
				      &probe->plic_size);
		else if (strcmp(name, "phandle") == 0) {
			if (!fdt_read_u32(value, len, &plic_phandle))
				panic("invalid PLIC phandle");
		} else if (strcmp(name, "riscv,ndev") == 0) {
			if (!fdt_read_u32(value, len, &probe->plic_ndev))
				panic("invalid PLIC ndev");
		} else if (strcmp(name, "interrupts-extended") == 0) {
			const u8 *p = value;

			/* QEMU 的 cpu-intc 使用一个中断 cell：每项是 phandle、cause。 */
			if (!len || len % 8)
				panic("invalid PLIC interrupts-extended");
			for (u32 off = 0; off < len; off += 8) {
				u32 cause;

				if (!fdt_read_u32(p + off + 4, 4, &cause))
					panic("invalid PLIC interrupt cause");
				if (cause == 9) { /* S 模式外部中断 */
					if (probe->plic_s_context != ~0U)
						panic("multiple PLIC S contexts");
					probe->plic_s_context = off / 8;
				}
			}
		}
	} else if (in_uart) {
		if (strcmp(name, "compatible") == 0)
			uart_compatible = compatible_has(value, len, "ns16550a");
		else if (strcmp(name, "reg") == 0)
			read_mmio_reg(value, len, &probe->uart_base,
				      &probe->uart_size);
		else if (strcmp(name, "interrupt-parent") == 0) {
			if (!fdt_read_u32(value, len, &uart_interrupt_parent))
				panic("invalid UART interrupt-parent");
		} else if (strcmp(name, "interrupts") == 0) {
			if (!fdt_read_u32(value, len, &probe->uart_irq))
				panic("invalid UART interrupts");
		}
	}
}

void virt_probe_irq_devices(const void *dtb, unsigned long hartid,
			    struct virt_irq_devices *devices)
{
	if (hartid != 0 || !devices)
		panic("only QEMU virt hart 0 is supported");
	memset(devices, 0, sizeof(*devices));
	probe = devices;
	devices->plic_s_context = ~0U;
	/* DT 规范的默认值；QEMU /soc 会显式覆盖为 2、2。 */
	soc_address_cells = 2;
	soc_size_cells = 1;
	if (!fdt_walk(dtb, probe_node, probe_property, NULL))
		panic("invalid FDT while probing IRQ devices");
	if (!soc_has_ranges || !plic_compatible || !uart_compatible ||
	    !plic_phandle || uart_interrupt_parent != plic_phandle ||
	    !devices->plic_base || !devices->uart_base ||
	    devices->plic_s_context == ~0U ||
	    !devices->plic_ndev || !devices->uart_irq ||
	    devices->uart_irq > devices->plic_ndev ||
	    devices->uart_size < 8 || devices->plic_size < 0x200008UL +
						devices->plic_s_context * 0x1000UL)
		panic("missing or unsupported QEMU virt IRQ devices");
	probe = NULL;
}
