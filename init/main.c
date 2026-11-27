#include <asm/plic.h>
#include <asm/sbi.h>
#include <asm/timer.h>
#include <asm/uart.h>
#include <asm/virt.h>
#include <minux/buddy.h>
#include <minux/block.h>
#include <minux/errno.h>
#include <minux/fs.h>
#include <minux/elf.h>
#include <minux/init.h>
#include <minux/irq.h>
#include <minux/memblock.h>
#include <minux/page.h>
#include <minux/panic.h>
#include <minux/printk.h>
#include <minux/selftest.h>
#include <minux/slab.h>
#include <minux/task.h>

struct block_device *boot_disk;

static bool log_free_range(u64 start_pfn, u64 end_pfn, void *data)
{
	(void)data;
	pr_debug("free RAM: [0x%llx, 0x%llx)\n",
		 start_pfn * PAGE_SIZE, end_pfn * PAGE_SIZE);
	return true;
}

void start_kernel(void)
{
	const struct virt_devices *devices = &virt_boot.devices;
	u32 timebase_frequency = virt_boot.timebase_frequency;
	u64 ram_pages = memblock_memory_pages(&boot_memblock);
	u64 nr_free_pages;
	int err;

	selftest_report_boot_reservations();
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
	err = virt_probe_boot_disk(devices, timebase_frequency, &boot_disk);
	if (!err)
		pr_info("virtio-blk: %llu sectors of %u bytes, read-only\n",
			boot_disk->sectors, BLK_SECTOR_SIZE);
	else if (err != -ENODEV)
		pr_err("virtio-blk: probe failed: %d\n", err);
	err = rootfs_init();
	if (err)
		panic("cannot initialize root filesystem: %d", err);
	run_rootfs_selftests();

	/* 打开总中断前配置 PLIC 和 UART。 */
	pr_info("PLIC: base=0x%lx, S context=%u; UART: base=0x%lx, IRQ=%u\n",
		devices->irq.plic_base, devices->irq.plic_s_context,
		devices->irq.uart_base, devices->irq.uart_irq);
	plic_init(&devices->irq);
	uart_init(&devices->irq);

	timer_init(timebase_frequency);
	arch_local_irq_enable();
	run_runtime_selftests();
	/* 首次派发同时建立 idle，确保 /init 能立刻阻塞等待子进程。 */
	unsigned long flags = arch_local_irq_save();
	err = start_init_process();

	if (err)
		panic("cannot start /init: %d", err);
	task_idle_once();
	arch_local_irq_restore(flags);
	pr_info("timer: %u Hz, timebase: %u Hz\n",
		TIMER_HZ, timebase_frequency);

#ifdef CONFIG_SELFTEST
	/* 自测通过周期日志确认串口中断结束后时钟仍在运行。 */
	for (unsigned long reported = 0;;) {
		unsigned long current;

		task_idle_once();
		current = timer_ticks();
		if (current < reported + TIMER_HZ)
			continue;
		reported = current;
		pr_info("timer: %lu ticks\n", current);
		/* 自测镜像观察两次周期报告后关机，便于 make test 完成。 */
		if (current >= 2 * TIMER_HZ)
			sbi_shutdown();
	}
#else
	for (;;)
		task_idle_once();
#endif
}
