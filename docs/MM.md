# v0.3 物理内存管理

启动顺序为 FDT → memblock → 启动页表 → buddy → slab。memblock 按物理地址区间整理可用 RAM，buddy 按页管理空闲块，slab 在这些页上分配内核对象。

## memblock 与 buddy

`start_kernel()` 从 FDT 登记 RAM 和固件保留区，同时保留内核镜像、DTB 与 FDT 保留表列出的范围。RAM 只计完整页，保留区按页向外取整。`buddy_init()` 从空闲页中划出页描述符数组，将它登记到保留表，再接管剩余页。页描述符按 RAM 段组织，物理地址空洞无需描述符。[FDT 早期读取](FDT.md)

buddy 为每个阶维护一条空闲链表。`order` 阶的块包含 `2^order` 个连续页，首帧按块大小对齐，v0.3 支持 0 至 10 阶。`alloc_pages(order)` 优先取同阶空闲块，必要时拆分更高阶的块。`free_pages(page, order)` 检查首帧和原阶数，再尝试与同一 RAM 段内同阶的空闲伙伴合并。

## slab 与对象分配

`slab_init()` 在 buddy 就绪后初始化各档位，首次需要槽位时才向 buddy 要页。档位从 16 B 起按 2 的幂增长，最大档为页大小的四分之一。RISC-V 当前页大小为 4 KiB，因此小对象使用 16 B 至 1 KiB 的档位。`kmalloc(63)` 取一个 64 B 槽，`kmalloc(3000)` 则从 buddy 取一个 0 阶块。

每个档位的 `partial` 链表只收录还有空槽的页。每张 slab 页在页首记录所属档位、空槽链表、已用槽数和分配位图，槽位从页尾向前排列。分配时取出一个空槽，并在该页分满时将它从 `partial` 摘下。释放时用 `virt_to_page()` 找到页，通过地址差算出槽号，检查并更新位图，再把空槽插回页内链表。满页释放出第一个槽时重新挂回 `partial`，最后一个对象释放时整页归还 buddy。

超出最大档位的请求直接由 buddy 提供能容纳请求的最小阶块。`kfree()` 根据首帧的归属与阶数识别并归还整个块。`kzalloc()` 清零请求的字节数，分配失败返回 `NULL`。

## 架构接口与使用范围

每个架构提供页大小和 RAM 线性映射。buddy 用 `page_address()` 和 `virt_to_page()` 向上层提供页与地址的转换。线性映射须在 buddy 初始化前可用，保留页内偏移和块内地址的连续性，并在分配器运行期间保持有效。

RISC-V 的 `paging_init()` 从 memblock 分配并保留 Sv39 页表页，为内核镜像、可用 RAM 和设备树中探测到的 PLIC、UART 寄存器建立高半区映射。代码段可读可执行，只读数据段只可读，数据段、可用 RAM 和设备寄存器可读写。每次选用虚实地址均对齐且能放入当前区间的最大叶项，支持 4 KiB、2 MiB 和 1 GiB。启动代码在打开分页前完成 DTB 读取，切换到高地址的代码、栈和异常入口后撤销临时恒等映射。

Minux 沿用 Linux 的启动区间管理和对象分配接口命名。Linux 用 GFP 标志表达分配上下文和回收策略。v0.3 面向单 hart 的普通内核上下文，`kmalloc()` 只接收请求大小。[Linux memblock 文档](https://docs.kernel.org/6.12/core-api/boot-time-mm.html) · [Linux 内核内存分配指南](https://docs.kernel.org/6.12/core-api/memory-allocation.html)

## 验收

运行 `make test` 启动 QEMU 自测内核。Sv39 自测覆盖三级页表遍历、分配失败回滚，以及真实的缺页和权限异常。buddy 自测核对启动后的空闲区间与各阶块数，并验证耗尽和归还后的统计。slab 自测覆盖档位选择、满页复用、大块分配和页回收。启动日志中的 `reserved` 包含内核、DTB、固件、启动页表及页描述符占用的 RAM 页。
