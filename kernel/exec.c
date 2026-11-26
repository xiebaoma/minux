#include <asm/elf.h>
#include <minux/buddy.h>
#include <minux/elf.h>
#include <minux/errno.h>
#include <minux/fs.h>
#include <minux/fcntl.h>
#include <minux/irq.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/printk.h>
#include <minux/slab.h>
#include <minux/string.h>
#include <minux/switch_to.h>
#include <minux/syscall.h>
#include <minux/task.h>
#include <minux/uaccess.h>

#define EXEC_MAX_STRINGS 32
struct exec_args {
	const char *argv[EXEC_MAX_STRINGS + 1], *envp[EXEC_MAX_STRINGS + 1];
	char strings[3072];
	size_t used;
};

struct exec_image {
	struct mm_struct *mm;
	uintptr_t pc, sp;		//`pc` 是入口地址，`sp` 是栈指针。
};

static int copy_vector(struct exec_args *args, uintptr_t vec, const char **out)
{
	for (size_t i = 0; vec; i++, vec += sizeof(uintptr_t)) {
		uintptr_t ptr;
		long len;

		if (copy_from_user(&ptr, vec, sizeof(ptr)))
			return -EFAULT;
		if (!ptr)
			break;
		if (i == EXEC_MAX_STRINGS)
			return -E2BIG;
		len = copy_string_from_user(args->strings + args->used, ptr,
					    sizeof(args->strings) - args->used);
		if (len < 0)
			return len;
		out[i] = args->strings + args->used;
		args->used += len;
	}
	return 0;
}

static int build_stack(struct exec_image *img, const char *const argv[], const char *const envp[])
{
	uintptr_t base = arch_user_limit() - PAGE_SIZE;
	struct page *page = mm_populate_page(img->mm, base);
	const char *const *vectors[] = { argv, envp };
	size_t top = PAGE_SIZE, count = 1;
	uintptr_t *words;
	char *bytes;

	if (!page)
		return -ENOMEM;
	bytes = page_address(page);
	words = (uintptr_t *)bytes;
	/* 在页底暂存指针表，从页顶向下放字符串，最后将表移到对齐后的 sp。 */
	for (size_t v = 0; v < 2; v++) {
		for (const char *const *s = vectors[v]; *s; s++) {
			size_t len = strlen(*s) + 1;

			if (len > top || (count + 1) * sizeof(*words) > top - len)
				return -E2BIG;
			top -= len;
			memcpy(bytes + top, *s, len);
			words[count++] = base + top;
		}
		if ((count + 1) * sizeof(*words) > top)
			return -E2BIG;
		if (!v)
			words[0] = count - 1;
		words[count++] = 0;
	}
	const uintptr_t auxv[] = { 6, PAGE_SIZE, 9, img->pc, 0, 0 }; /* AT_PAGESZ, AT_ENTRY, AT_NULL */
	size_t table_size = count * sizeof(*words) + sizeof(auxv);

	if (table_size > top)
		return -E2BIG;
	memcpy(words + count, auxv, sizeof(auxv));
	top = (top - table_size) & ~(uintptr_t)(ARCH_STACK_ALIGN - 1);
	memmove(bytes + top, bytes, table_size);
	img->sp = base + top;
	return 0;
}

static int prepare_image(const char *path, const char *const argv[], const char *const envp[],
			 struct exec_image *img)
{
	struct file file;
	int err = vfs_open(root_inode, path, &file);

	if (err)
		return err;
	err = elf_load(&file, &img->mm, &img->pc);
	if (err)
		return err;
	err = build_stack(img, argv, envp);
	if (err)
		mm_destroy(img->mm);
	return err;
}

int start_init_process(void)
{
	const char *const argv[] = { "/init", NULL }, *const envp[] = { NULL };
	struct exec_image img;
	struct task_struct *task;
	struct fdtable files = {0};
	int err = prepare_image(argv[0], argv, envp, &img);

	if (err)
		return err;
	for (int fd = 0; fd < 3; fd++) {
		err = file_open("/dev/console", fd == 0 ? O_RDONLY : O_WRONLY, &files.slots[fd]);
		if (err)
			goto fail;
	}
	/* 首次派发前移交 stdin/stdout/stderr，此后沿用普通 fd 生命周期。 */
	unsigned long irq = arch_local_irq_save();
	task = task_create_user(img.mm, img.pc, img.sp);
	if (!task) {
		arch_local_irq_restore(irq);
		err = -ENOMEM;
		goto fail;
	}
	task->files = files;
	pr_info("user task %d: start /init\n", task->pid);
	arch_local_irq_restore(irq);
	return 0;
fail:
	fdtable_release(&files);
	mm_destroy(img.mm);
	return err;
}

long sys_execve(uintptr_t filename, uintptr_t argv, uintptr_t envp, struct pt_regs *regs)
{
	struct task_struct *task = task_current();
	struct exec_image img;
	struct exec_args *args;
	struct mm_struct *old;
	unsigned long flags;

	//从旧地址空间取出文件名
	char path[VFS_PATH_MAX];
	long err = copy_string_from_user(path, filename, sizeof(path));

	if (err < 0)
		return err == -E2BIG ? -ENAMETOOLONG : err;

	//把 argv 和 envp 连同字符串一起复制到内核
	args = kzalloc(sizeof(*args));
	if (!args)
		return -ENOMEM;
	err = copy_vector(args, argv, args->argv);
	if (!err)
		err = copy_vector(args, envp, args->envp);
	if (!err) {
		/* 空参数向量也提供一个可用的 argv[0]。 */
		if (!args->argv[0])
			args->argv[0] = "";
		//在独立地址空间中把新程序准备好
		err = prepare_image(path, args->argv, args->envp, &img);
	}
	kfree(args);
	if (err)
		return err;

	/* 提交点之后均为不可失败的操作。寄存器、PID 和页表在同一任务上生效。 */
	//提交新映像，改写这次 trap 的返回目的地
	flags = arch_local_irq_save();
	old = task->mm;
	task->mm = img.mm;
	arch_pgd_activate(img.mm->pgd);
	arch_setup_user_regs(regs, img.pc, img.sp);
	mm_destroy(old);
	pr_info("user task %d: exec %s\n", task->pid, path);
	arch_local_irq_restore(flags);
	return 0;
}
