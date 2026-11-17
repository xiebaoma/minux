#include <asm/sbi.h>
#include <asm/timer.h>
#include <asm/trap.h>
#include <minux/fdt.h>
#include <minux/limits.h>
#include <minux/panic.h>
#include <minux/printk.h>
#include <minux/selftest.h>
#include <minux/string.h>

/* kernel.ld 给出的镜像边界；当前未启用分页，链接地址就是物理地址。 */
extern char _kernel_start[];
extern char _kernel_end[];

/* 设备树规范规定：父节点没有写这两个属性时，默认分别为 2 和 1。 */
static u32 root_address_cells = 2;
static u32 root_size_cells = 1;
static u32 reserved_address_cells;
static u32 reserved_size_cells;
static bool current_root_child_is_memory;
static bool current_root_child_is_reserved;
static bool current_root_child_is_cpus;
static bool found_memory;
static u32 timebase_frequency;
static unsigned int fdt_reservation_count;
static unsigned int reserved_region_count;

static void early_puts(const char *s)
{
	while (*s)
		sbi_console_putchar(*s++);
}

static void log_fdt_node(const char *name, unsigned int depth)
{
	/* 调试时展示根、第一层节点，以及 /reserved-memory 的子节点。 */
	if (depth <= 1 || (depth == 2 && current_root_child_is_reserved))
		pr_debug("FDT node: %s\n", depth == 0 ? "/" : name);
	if (depth == 1) {
		current_root_child_is_memory =
			strcmp(name, "memory") == 0 ||
			strncmp(name, "memory@", 7) == 0;
		current_root_child_is_reserved =
			strcmp(name, "reserved-memory") == 0;
			current_root_child_is_cpus = strcmp(name, "cpus") == 0;
		if (current_root_child_is_reserved) {
			reserved_address_cells = root_address_cells;
			reserved_size_cells = root_size_cells;
		}
	}
}

static unsigned int print_reg_ranges(const char *kind, const void *value,
				     u32 len, u32 address_cells,
				     u32 size_cells)
{
	const u8 *p = value;
	u32 address_bytes, size_bytes, entry_bytes;
	unsigned int count = 0;

	/* v0.1 只需处理能装入 u64 的 1 或 2 个 cell。 */
	if (address_cells < 1 || address_cells > 2 ||
	    size_cells < 1 || size_cells > 2)
		panic("unsupported %s address/size cell count", kind);
	address_bytes = address_cells * 4;
	size_bytes = size_cells * 4;
	entry_bytes = address_bytes + size_bytes;
	if (len == 0 || len % entry_bytes != 0)
		panic("invalid %s reg length: %u", kind, len);

	/* reg 可以包含多组 <起点, 长度>。 */
	for (u32 off = 0; off < len; off += entry_bytes) {
		u64 start, bytes, end;

		if (!fdt_read_cells(p + off, address_bytes,
				    address_cells, &start) ||
		    !fdt_read_cells(p + off + address_bytes, size_bytes,
				    size_cells, &bytes))
			panic("invalid %s reg cells", kind);
		if (bytes == 0 || start > ULLONG_MAX - bytes)
			panic("invalid %s range", kind);
		end = start + bytes;
		pr_info("%s: [0x%llx, 0x%llx), %llu bytes\n",
			kind, start, end, bytes);
		count++;
	}
	return count;
}

static void log_fdt_property(const char *name, const void *value,
			     u32 len, unsigned int depth)
{
	/* 调试时展示属性的原始位置；这里只解释内存和时钟频率。 */
	if (depth <= 1 || (depth == 2 && current_root_child_is_reserved))
		pr_debug("  FDT property: %s, value=%p, len=%u\n",
			name, value, len);
	if (depth == 1 && current_root_child_is_cpus &&
	    strcmp(name, "timebase-frequency") == 0) {
		if (!fdt_read_u32(value, len, &timebase_frequency) ||
		    !timebase_frequency)
			panic("invalid /cpus/timebase-frequency");
		return;
	}
	if (depth == 2 && current_root_child_is_reserved &&
	    strcmp(name, "reg") == 0) {
		reserved_region_count += print_reg_ranges("FDT reserved-memory",
			value, len, reserved_address_cells, reserved_size_cells);
		return;
	}
	if (depth == 1 && current_root_child_is_memory &&
	    strcmp(name, "reg") == 0) {
		print_reg_ranges("physical RAM", value, len,
				 root_address_cells, root_size_cells);
		found_memory = true;
		return;
	}
	if (depth == 1 && current_root_child_is_reserved) {
		if (strcmp(name, "#address-cells") == 0) {
			if (!fdt_read_u32(value, len, &reserved_address_cells))
				panic("invalid reserved-memory #address-cells");
		} else if (strcmp(name, "#size-cells") == 0) {
			if (!fdt_read_u32(value, len, &reserved_size_cells))
				panic("invalid reserved-memory #size-cells");
		} else if (strcmp(name, "ranges") == 0 && len != 0) {
			/* 非空 ranges 需要地址转换；v0.1 不猜测转换结果。 */
			panic("unsupported reserved-memory address translation");
		}
		return;
	}

	/* 根节点的 cell 数量决定其直接子节点的 reg 如何分组。 */
	if (depth != 0)
		return;
	if (strcmp(name, "#address-cells") == 0) {
		if (!fdt_read_u32(value, len, &root_address_cells))
			panic("invalid root #address-cells");
	} else if (strcmp(name, "#size-cells") == 0) {
		if (!fdt_read_u32(value, len, &root_size_cells))
			panic("invalid root #size-cells");
	}
}

static void log_fdt_reservation(u64 address, u64 size)
{
	pr_info("FDT reserved: [0x%llx, 0x%llx)\n",
		address, address + size);
	fdt_reservation_count++;
}

void start_kernel(unsigned long hartid, unsigned long dtb_pa)
{
	u32 dtb_size;
	unsigned long kernel_start = (unsigned long)_kernel_start;
	unsigned long kernel_end = (unsigned long)_kernel_end;

	/* 尽早安装 trap 入口，使后续启动故障可以报告现场。 */
	trap_init();
	early_puts("Hello from Minux!\n");
	pr_info("boot hart: %lu\n", hartid);
	if (!fdt_check_header((const void *)dtb_pa, &dtb_size))
		panic("invalid FDT header at %p", (const void *)dtb_pa);
	if (kernel_end <= kernel_start || dtb_pa > ULONG_MAX - dtb_size)
		panic("invalid kernel or DTB address range");
	pr_debug("FDT: address=%p, size=%u bytes\n",
		(const void *)dtb_pa, dtb_size);
	pr_info("kernel image: [0x%lx, 0x%lx)\n",
		kernel_start, kernel_end);
	pr_info("DTB storage: [0x%lx, 0x%lx)\n",
		dtb_pa, dtb_pa + dtb_size);
	if (!fdt_walk((const void *)dtb_pa, log_fdt_node,
		      log_fdt_property))
		panic("invalid FDT structure");
	if (!found_memory)
		panic("no memory reg in FDT");
	if (!timebase_frequency)
		panic("no /cpus/timebase-frequency in FDT");
	pr_debug("FDT root: #address-cells=%u, #size-cells=%u\n",
		root_address_cells, root_size_cells);
	if (!fdt_walk_reservations((const void *)dtb_pa,
				   log_fdt_reservation))
		panic("invalid FDT memory reservation block");
	pr_debug("FDT memory reservation entries: %u\n", fdt_reservation_count);
	pr_debug("FDT reserved-memory ranges: %u\n", reserved_region_count);

	/* 只在 `make test` 构建时有效，普通构建下是空函数 */
	run_selftests();

	pr_info("timer: %u Hz, timebase: %u Hz\n",
		TIMER_HZ, timebase_frequency);
	timer_init(timebase_frequency);
	trap_enable_interrupts();

	/* 中断处理器只计数；普通上下文每秒报告一次，避免在中断中刷屏。 */
	for (unsigned long reported = 0;;) {
		unsigned long current;

		asm volatile("wfi" : : : "memory");
		current = timer_ticks();
		if (current < reported + TIMER_HZ)
			continue;
		reported = current;
		pr_info("timer: %lu ticks\n", current);
#ifdef CONFIG_SELFTEST
		/* 自测镜像观察两次周期报告后关机，便于 make test 完成。 */
		if (current >= 2 * TIMER_HZ)
			sbi_shutdown();
#endif
	}
}
