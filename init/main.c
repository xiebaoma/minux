#include <asm/plic.h>
#include <asm/sbi.h>
#include <asm/timer.h>
#include <asm/trap.h>
#include <asm/uart.h>
#include <asm/virt.h>
#include <minux/buddy.h>
#include <minux/fdt.h>
#include <minux/limits.h>
#include <minux/memblock.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/panic.h>
#include <minux/printk.h>
#include <minux/selftest.h>
#include <minux/slab.h>
#include <minux/string.h>
#include <minux/task.h>

/* 设备树规范规定：父节点没有写这两个属性时，默认分别为 2 和 1。 */
static u32 root_address_cells = 2;
static u32 root_size_cells = 1;
static u32 reserved_address_cells;
static u32 reserved_size_cells;
static bool current_root_child_is_memory;
static bool current_root_child_is_reserved;
static bool current_root_child_is_cpus;
static bool reserved_available;
static bool reserved_needs_translation;
static u32 timebase_frequency;
static struct virt_irq_devices irq_devices;

static struct memory_node {
	const char *name;
	const void *reg;
	u32 reg_len;
	bool has_size;
	bool available;
} memory_node;

static void early_puts(const char *s)
{
	while (*s)
		sbi_console_putchar(*s++);
}

static void scan_fdt_node(const char *name, unsigned int depth)
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
			reserved_available = true;
			reserved_needs_translation = false;
		}
	}
	if ((depth == 1 && current_root_child_is_memory) ||
	    (depth == 2 && current_root_child_is_reserved))
		memory_node = (struct memory_node){ .name = name, .available = true };
}

static void add_reg_ranges(const void *value, u32 len, u32 address_cells,
			   u32 size_cells, bool reserved)
{
	const char *kind = reserved ? "FDT reserved-memory" : "physical RAM";
	const u8 *p = value;
	u32 address_bytes, size_bytes, entry_bytes;

	/* 当前只处理能装入 u64 的 1 或 2 个 cell。 */
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
		u64 start, bytes;

		if (!fdt_read_cells(p + off, address_bytes,
				    address_cells, &start) ||
		    !fdt_read_cells(p + off + address_bytes, size_bytes,
				    size_cells, &bytes))
			panic("invalid %s reg cells", kind);
		if (bytes == 0 || start > ULLONG_MAX - bytes)
			panic("invalid %s range", kind);
		if (!(reserved ? memblock_reserve(&boot_memblock, start, bytes) :
				 memblock_add(&boot_memblock, start, bytes)))
			panic("boot memory table full while recording %s", kind);
		pr_info("%s: [0x%llx, 0x%llx), %llu bytes\n",
			kind, start, start + bytes, bytes);
	}
}

static bool fdt_status_available(const void *value, u32 len)
{
	return (len == sizeof("okay") && memcmp(value, "okay", len) == 0) ||
	       (len == sizeof("ok") && memcmp(value, "ok", len) == 0);
}

static void scan_fdt_property(const char *name, const void *value,
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
	if ((depth == 1 && current_root_child_is_memory) ||
	    (depth == 2 && current_root_child_is_reserved)) {
		if (strcmp(name, "reg") == 0) {
			memory_node.reg = value;
			memory_node.reg_len = len;
		} else if (strcmp(name, "size") == 0) {
			memory_node.has_size = true;
		} else if (strcmp(name, "status") == 0) {
			memory_node.available = fdt_status_available(value, len);
		}
		return;
	}
	if (depth == 1 && current_root_child_is_reserved) {
		if (strcmp(name, "#address-cells") == 0) {
			if (!fdt_read_u32(value, len, &reserved_address_cells))
				panic("invalid reserved-memory #address-cells");
		} else if (strcmp(name, "#size-cells") == 0) {
			if (!fdt_read_u32(value, len, &reserved_size_cells))
				panic("invalid reserved-memory #size-cells");
		} else if (strcmp(name, "ranges") == 0) {
			reserved_needs_translation = len != 0;
		} else if (strcmp(name, "status") == 0) {
			reserved_available = fdt_status_available(value, len);
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

static void finish_fdt_node(unsigned int depth)
{
	if (depth == 1 && current_root_child_is_reserved && reserved_available &&
	    reserved_needs_translation)
		panic("unsupported reserved-memory address translation");

	if (depth == 1 && current_root_child_is_memory && memory_node.available &&
	    memory_node.reg)
		add_reg_ranges(memory_node.reg, memory_node.reg_len,
			       root_address_cells, root_size_cells, false);
	if (depth != 2 || !current_root_child_is_reserved || !reserved_available ||
	    !memory_node.available)
		return;

	/* 属性顺序任意。同一节点同时有 reg 和 size 时，以 reg 为准。 */
	if (memory_node.reg)
		add_reg_ranges(memory_node.reg, memory_node.reg_len,
			       reserved_address_cells, reserved_size_cells, true);
	else if (memory_node.has_size)
		panic("unsupported dynamic /reserved-memory/%s (size without reg)",
		      memory_node.name);
	else
		panic("missing reg or size in /reserved-memory/%s", memory_node.name);
}

static void reserve_fdt_range(u64 address, u64 size)
{
	if (!memblock_reserve(&boot_memblock, address, size))
		panic("boot memory table full while recording FDT reservations");
	pr_info("FDT reserved: [0x%llx, 0x%llx)\n",
		address, address + size);
}

static bool log_free_range(u64 start_pfn, u64 end_pfn, void *data)
{
	(void)data;
	pr_debug("free RAM: [0x%llx, 0x%llx)\n",
		 start_pfn * PAGE_SIZE, end_pfn * PAGE_SIZE);
	return true;
}

static void start_kernel_mapped(void) __noreturn;

void start_kernel(unsigned long hartid, unsigned long dtb_pa,
		  unsigned long kernel_start, unsigned long kernel_end)
{
	u32 dtb_size;

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

	/* 先登记内核镜像和 DTB 本身占用的物理地址。 */
	if (!memblock_reserve(&boot_memblock, kernel_start, kernel_end - kernel_start) ||
	    !memblock_reserve(&boot_memblock, dtb_pa, dtb_size))
		panic("cannot reserve kernel or DTB in boot memory table");

	/* 遍历设备树节点：memory 提供 RAM，/reserved-memory 提供保留区。 */
	if (!fdt_walk((const void *)dtb_pa, scan_fdt_node, scan_fdt_property,
		      finish_fdt_node))
		panic("invalid FDT structure");
	if (!memblock_memory_pages(&boot_memblock))
		panic("no full physical RAM pages in FDT");
	if (!timebase_frequency)
		panic("no /cpus/timebase-frequency in FDT");
	pr_debug("FDT root: #address-cells=%u, #size-cells=%u\n",
		root_address_cells, root_size_cells);

	/* DTB 头部还指向一张独立的内存保留表，把其中的范围也登记进去。 */
	if (!fdt_walk_reservations((const void *)dtb_pa,
				   reserve_fdt_range))
		panic("invalid FDT memory reservation block");

	/* 用完 DTB 后，在交给 buddy 前为启动页表保留物理页。 */
	virt_probe_irq_devices((const void *)dtb_pa, hartid, &irq_devices);
	const struct memblock_region mmio[] = {
		{ irq_devices.plic_base, irq_devices.plic_base + irq_devices.plic_size },
		{ irq_devices.uart_base, irq_devices.uart_base + irq_devices.uart_size },
	};
	paging_init(&boot_memblock, mmio, sizeof(mmio) / sizeof(mmio[0]),
		    start_kernel_mapped);
}

static void start_kernel_mapped(void)
{
	u64 ram_pages = memblock_memory_pages(&boot_memblock);
	u64 nr_free_pages;

	if (!buddy_init(&boot_memblock))
		panic("cannot initialize buddy allocator");
	slab_init();
	task_init();
	memblock_for_each_free_range(&boot_memblock, log_free_range, NULL);
	nr_free_pages = buddy_free_pages();
	pr_info("memory: %llu RAM pages, %llu reserved, %llu free\n",
		ram_pages, ram_pages - nr_free_pages, nr_free_pages);

	/* 只在 `make test` 构建时有效，普通构建下是空函数 */
	run_selftests();

	/* 打开总中断前配置 PLIC 和 UART。 */
	pr_info("PLIC: base=0x%lx, S context=%u; UART: base=0x%lx, IRQ=%u\n",
		irq_devices.plic_base, irq_devices.plic_s_context,
		irq_devices.uart_base, irq_devices.uart_irq);
	plic_init(&irq_devices);
	uart_init(&irq_devices);

	pr_info("timer: %u Hz, timebase: %u Hz\n",
		TIMER_HZ, timebase_frequency);
	timer_init(timebase_frequency);
	trap_enable_interrupts();

	/* 中断处理器只计数；普通上下文每秒报告一次，避免在中断中刷屏。 */
	for (unsigned long reported = 0;;) {
		unsigned long current;

		task_idle_once();
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
