# 内存管理

启动顺序为 FDT → memblock → 启动页表 → buddy → slab。memblock 按物理地址区间整理可用 RAM，buddy 按页管理空闲块，slab 在这些页上分配内核对象。

## memblock 与 buddy

`riscv_boot_kernel()` 从 FDT 登记 RAM 和固件保留区，同时保留内核镜像、DTB 与 FDT 保留表列出的范围。RAM 只计完整页，保留区按页向外取整。`buddy_init()` 从空闲页中划出页描述符数组，将它登记到保留表，再接管剩余页。页描述符按 RAM 段组织，物理地址空洞无需描述符。[FDT 早期读取](FDT.md)

buddy 为每个阶维护一条空闲链表。`order` 阶的块包含 `2^order` 个连续页，首帧按块大小对齐，v0.3 支持 0 至 10 阶。`alloc_pages(order)` 优先取同阶空闲块，必要时拆分更高阶的块。`free_pages(page, order)` 检查首帧和原阶数，再尝试与同一 RAM 段内同阶的空闲伙伴合并。

## slab 与对象分配

`slab_init()` 在 buddy 就绪后初始化各档位，首次需要槽位时才向 buddy 要页。档位从 16 B 起按 2 的幂增长，最大档为页大小的四分之一。RISC-V 当前页大小为 4 KiB，因此小对象使用 16 B 至 1 KiB 的档位。`kmalloc(63)` 取一个 64 B 槽，`kmalloc(3000)` 则从 buddy 取一个 0 阶块。

每个档位的 `partial` 链表只收录还有空槽的页。每张 slab 页在页首记录所属档位、空槽链表、已用槽数和分配位图，槽位从页尾向前排列。分配时取出一个空槽，并在该页分满时将它从 `partial` 摘下。释放时用 `virt_to_page()` 找到页，通过地址差算出槽号，检查并更新位图，再把空槽插回页内链表。满页释放出第一个槽时重新挂回 `partial`，最后一个对象释放时整页归还 buddy。

超出最大档位的请求直接由 buddy 提供能容纳请求的最小阶块。`kfree()` 根据首帧的归属与阶数识别并归还整个块。`kzalloc()` 清零请求的字节数，分配失败返回 `NULL`。

## 架构接口与使用范围

每个架构提供页大小和 RAM 线性映射。buddy 用 `page_address()` 和 `virt_to_page()` 向上层提供页与地址的转换。线性映射须在 buddy 初始化前可用，保留页内偏移和块内地址的连续性，并在分配器运行期间保持有效。

Minux 沿用 Linux 的启动区间管理和对象分配接口命名。Linux 用 GFP 标志表达分配上下文和回收策略。v0.3 面向单 hart 的普通内核上下文，`kmalloc()` 只接收请求大小。[Linux memblock 文档](https://docs.kernel.org/6.12/core-api/boot-time-mm.html) · [Linux 内核内存分配指南](https://docs.kernel.org/6.12/core-api/memory-allocation.html)

## v0.4 虚拟内存

RISC-V 的 `paging_init()` 从 memblock 分配并保留 Sv39 页表页，为内核镜像、可用 RAM 和设备树中探测到的 PLIC、UART 寄存器建立高半区映射。代码段可读可执行，只读数据段只可读，数据段、可用 RAM 和设备寄存器可读写。每次选用虚实地址均对齐且能放入当前区间的最大叶项，支持 4 KiB、2 MiB 和 1 GiB。启动代码在打开分页前完成 DTB 读取，切换到高地址的代码、栈和异常入口后撤销临时恒等映射。

每个 `mm_struct` 持有独立的根页表和一条按地址排序的 VMA 链表。`mm_alloc()` 通过架构接口从 buddy 分配根表。Sv39 后端复制启动根表的高半区表项，使内核子页表在各地址空间之间共享。低半区从空表开始，用户映射使用 4 KiB 页。

`mm_add_vma()` 登记页对齐的 `[vm_start, vm_end)` 区间和访问权限，允许区间相邻，拒绝重叠。`find_vma()` 返回第一个满足 `addr < vm_end` 的 VMA，调用者通过 `vm_start <= addr` 判断地址是否在该区间内。Minux 沿用 Linux 的查找语义，用有序链表管理首版的 VMA。 [Linux 地址空间文档](https://docs.kernel.org/6.13/mm/process_addrs.html)

`mm_populate_page()` 在 VMA 内为指定虚拟页分配并清零数据页，再由 Sv39 后端将权限编码到用户叶项。缺失的子页表按需分配，分配失败时归还本次取得的数据页和子表。`mm_destroy()` 遍历低半区，放下各数据页的引用，归还子表和根表，再释放 VMA 对象。登记 VMA 和销毁地址空间时，页表须处于未启用状态。

## v0.6 匿名缺页与用户内存拷贝

只有显式声明 `VM_ANON` 的 VMA 才会按需分配零页。缺页处理检查故障地址是否在 VMA 内、访问权限是否匹配，以及页表是否缺少映射。Sv39 后端安装用户映射后执行 `sfence.vma`，随后用户任务可重试原指令。

`copy_from_user()` 和 `copy_to_user()` 先检查整个地址区间是否溢出或超出用户空间，再逐页检查 VMA 和叶项权限。拷贝通过物理页的内核线性映射完成，必要时会补匿名页。接口返回未拷贝的字节数。读入失败时，`copy_from_user()` 将内核缓冲区剩余部分清零。自测镜像通过一个测试专用系统调用验证合法跨页拷贝与 `-EFAULT` 返回。

## v0.6 写时复制

`mm_dup()` 复制 VMA 和已存在的用户页表分支。每个用户叶项持有一个物理页引用，未触碰的匿名页仍可在父子进程中分别按需分配。Sv39 使用 RSW 软件位标记写时复制（COW）。只读代码页保持原权限。子页表全部建成后，才撤销父进程相应叶项的写权限，并刷新本 hart 的 TLB。分配失败时销毁尚未发布的子地址空间即可归还引用和页表页。

用户写缺页和 `copy_to_user()` 都会检查 COW。仍有其他映射时，内核复制整页，安装可写映射并刷新该地址的翻译，再放下旧引用。最后一个持有者可以在原页恢复写权限。复制时内存耗尽会保留原来的映射和数据。

## v0.6 静态 ELF 与初始栈

`/init` 来自内嵌的 initramfs，`/disk/bin/child` 来自只读 ext2。首次启动和 `execve` 共用同一个通过 VFS 读取文件的加载器。当前接受小端 ELF64、RV64 软浮点 ABI 的静态 `ET_EXEC`。加载器校验程序头、文件和用户地址范围、对齐以及可执行入口。各 `PT_LOAD` 的页范围按虚拟地址递增且互不重叠。文件字节按段内偏移复制，段尾 BSS 清零，整页 BSS 按需分配。段的 R/W/X 标志决定最终用户页权限。[ELF 程序头规范](https://gabi.xinuos.com/elf/07-pheader.html)

用户地址空间顶部预留 8 页可读写栈，下方留一页保护区。入口 `sp` 按 16 字节对齐，依次指向 `argc`、以空指针结尾的 `argv` 和 `envp`，以及含 `AT_PAGESZ`、`AT_ENTRY`、`AT_NULL` 的辅助向量。路径上限为 127 字节，参数和环境各最多 32 项，字符串合计上限为 3072 字节（含各自的 NUL）。内核从旧地址空间复制输入，在独立页表内完成装载和建栈，再切换根表并释放旧映像。失败时调用者在原程序内收到负 errno。

## v1.2 私有匿名映射与 VMA 拆分

`struct mm_struct` 用 `mmap_limit` 约束动态映射上界。`elf_load()` 将其设为栈保护页底部（`stack_start - PAGE_SIZE`），使自动选址和地址提示都不会侵入用户栈及下方 1 页保护区；`mm_dup()` 复制该上界。

`mm_mmap_anon()` 将请求长度向上按页取整，若调用者提供页对齐且落在 `[PAGE_SIZE, mmap_limit)` 内的空闲 `hint` 则优先采用，否则从 `mmap_limit` 自顶向下扫描 VMA 间隙并登记 `VM_ANON` 区间，物理页仍在首次访问时按需清零分配。`mm_munmap()` 处理完全覆盖删除、裁头、裁尾及中间挖空拆分四种情况；中间拆分先分配右侧新 VMA，内存不足时直接返回 `-ENOMEM` 而不改动原 VMA 与页表。页表层 `arch_pgd_unmap_range()` 清除区间内的用户叶项并放下物理页引用，当 Sv39 的 L0 或 L1 子表变空时先刷新翻译再立即归还空子表页。[VMA 管理](../mm/mm.c) · [Sv39 页表](../arch/riscv/sv39.c)

系统调用保持 Linux RV64 编号（`__NR_munmap = 215`，`__NR_mmap = 222`）：

- `mmap(addr, len, prot, flags, fd, offset)`：仅支持私有匿名映射，要求 `flags == (MAP_PRIVATE | MAP_ANONYMOUS)`、`fd == -1`、`offset == 0`、`len > 0`；`prot` 取 `PROT_READ`、`PROT_WRITE`、`PROT_EXEC` 的非空组合且可写须可读。`PROT_NONE`、只写、`MAP_SHARED`、`MAP_FIXED` 或未知位均返回 `-EINVAL`；长度溢出、虚拟区间不足或 VMA 分配失败返回 `-ENOMEM`。
- `munmap(addr, len)`：要求 `addr` 按页对齐、`len > 0` 且区间落在用户地址空间内不溢出，否则返回 `-EINVAL`；`len` 按页向上取整，未映射空洞直接忽略，中间拆分内存不足返回 `-ENOMEM`。[UAPI 常量](../include/uapi/minux/mman.h) · [系统调用实现](../kernel/syscall.c)

## v1.2 libc 错误码与用户态堆

libc 在内部保留返回内核原始负错误码且不修改 `errno` 的 `__syscall0`..`__syscall6`，公开系统调用包装（`open`、`read`、`write`、`close`、`dup3`、`pipe`、`fork`、`execve`、`waitpid`、`mmap`、`munmap`）统一经 `__syscall_ret()` 处理：当内核返回值落在 `[-4095, -1]` 时设置 `errno = -ret` 并返回 `-1`（`mmap` 返回 `MAP_FAILED`），成功调用不修改 `errno`。[错误转换](../user/libc/errno.c) · [系统调用包装](../user/libc/sys.c)

`malloc`、`free`、`realloc` 基于 `mmap`/`munmap` 构建，返回指针均按 16 字节对齐：

- 不超过 8 KiB 的请求从 16 KiB 的匿名 `arena` 中切分，释放时与前后相邻空闲块合并；整块 `arena` 完全空闲时从链表摘除并调用 `munmap` 归还内核。
- 超过 8 KiB 的大块按页向上取整独立 `mmap`，`free` 时直接 `munmap` 归还。
- `malloc(0)` 返回 `NULL` 且不修改 `errno`；`free(NULL)` 为空操作；`realloc(NULL, size)` 等价于 `malloc(size)`，`realloc(ptr, 0)` 释放原块并返回 `NULL`。大小溢出或映射失败时设置 `errno = ENOMEM` 并返回 `NULL`；`realloc` 缩容与后继空闲块合并原地完成，迁移失败时保留原指针及原数据不变。[堆分配器](../user/libc/malloc.c)

shell（`user/sh.c`）使用堆动态保存输入命令行、参数指针数组与重定向表，解除原 127 字节命令行长度限制；超出内核 `execve` 参数上限时由 `execve` 返回 `E2BIG` 并报错恢复。

## 验收

运行 `make test` 启动 QEMU 自测内核。Sv39 和地址空间自测覆盖三级页表遍历、动态解除映射与空子表回收、VMA 自动选址与拆分、分配失败回滚及写时复制。用户态测试 `/test/mmap` 与 `/test/commands` 覆盖 `errno` 转换、跨页匿名映射与 `fork` 写时复制、解除中间页后两侧读写与子进程 `SIGSEGV` 终止、反复堆分配扩容与页面全量回收，以及超过 127 字节的长命令行。buddy 自测核对启动后的空闲区间与各阶块数，并验证耗尽和归还后的统计。slab 自测覆盖档位选择、满页复用、大块分配和页回收。启动日志中的 `reserved` 包含内核、DTB、固件、启动页表及页描述符占用的 RAM 页。
