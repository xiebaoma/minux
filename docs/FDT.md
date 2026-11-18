# FDT 早期读取

## 目的与启动顺序

在当前 QEMU `virt` 启动路径中，OpenSBI 通过 `a0` 传入启动 hart 编号、通过 `a1` 传入 DTB 的物理地址。Minux 尚未启用分页：`head.S` 使用内核镜像中预留的启动栈，再调用 `start_kernel(hartid, dtb_pa)`。此时读取 DTB 不依赖物理内存分配器；启动链必须保证传入地址可读，并在解析期间保留 DTB 内容。

`start_kernel()` 检查 DTB 头部，登记内核镜像和 DTB 自身的地址区间，再遍历节点、属性和头部指向的内存保留表。所有区间均使用 `[起点, 终点)` 表示，终点不属于该区间。

## 数据如何读取

DTB 是一块连续的扁平字节数据，不是已经展开的 C 节点树。头部包含总长度，以及内存保留表、结构区和属性名字符串区的偏移。结构区以 `FDT_BEGIN_NODE`、`FDT_PROP`、`FDT_END_NODE` 等 token 表示层级；属性名通过字符串区偏移取得，属性值仍指向原 DTB 内的字节。

| 接口 | 当前用途 |
|---|---|
| `fdt_check_header()` | 检查 magic、版本、区块偏移、长度与对齐。 |
| `fdt_walk()` | 顺序遍历结构区，通过回调报告节点开始、节点属性和节点结束。 |
| `fdt_read_u32()` / `fdt_read_cells()` | 按大端读取一个或两个 32 位 cell。 |
| `fdt_walk_reservations()` | 遍历由 64 位地址、64 位长度组成的内存保留表。 |

`reg` 中每组地址和长度各占多少 cell，由**该节点的父节点**的 `#address-cells` 和 `#size-cells` 决定。根节点缺少它们时，按规范默认使用 2 和 1。当前 QEMU `virt` 根节点使用 2 和 2；例如内存节点的 `reg = <0 0x80000000 0 0x08000000>` 表示 `[0x80000000, 0x88000000)`，即 128 MiB。

内存保留表与 `/reserved-memory` 是两处不同的数据来源。前者在 DTB 头部指定的独立区块中；后者是树中的节点。当前 QEMU/OpenSBI 把两段 M 模式固件占用区放在 `/reserved-memory` 下。RAM 节点描述的是物理 RAM 总范围，包含其中被固件和内核占用的地址；不能直接当作空闲页列表。

## v0.3 启动内存表

`boot_memblock` 参照 Linux 的 `memblock`，分别记录 RAM 和保留区。两张表都按地址排序，合并相邻或重叠的区间，每张表最多容纳 32 个区间。表放在内核 BSS 中，属于内核镜像的保留范围。

- **RAM** 来自根节点下启用的 `memory` / `memory@...` 节点的 `reg`。
- **保留区** 来自启用的 `/reserved-memory` 子节点的静态 `reg`、FDT 头部指向的保留表、内核镜像和 DTB 自身。

`memblock_for_each_free_range()` 给出可交给页帧分配器的连续页帧区间。RAM 只计入完整页，保留区覆盖所有与它相交的页。`buddy_init()` 先用 `memblock_alloc_pages()` 分配并保留页描述符占用的内存，再接管剩余空闲页。启动日志从 memblock 读取 RAM 总页数，从 buddy 读取空闲页数，两者之差是保留页数。后续分配流程见[物理内存管理](MM.md)。

### 静态和动态保留区

静态 `reg` 直接给出物理起点和字节数。动态 `size` 给出要保留的字节数，由内核在启动时选定一段连续 RAM，还可以用 `alignment` 约束对齐、用 `alloc-ranges` 限定候选地址范围。两者同时存在时以 `reg` 为准。Minux 遇到启用且只有 `size` 的子节点会打印节点名并停机。 [Devicetree 规范](https://devicetree-specification.readthedocs.io/en/latest/chapter3-devicenodes.html#reserved-memory-child-nodes)

地址和长度分别支持 1 或 2 个 cell。启用的 `/reserved-memory` 若带非空 `ranges`，启动会报错停机。`no-map` 和 `reusable` 区域都从空闲页中扣除，映射和回收策略留待后续版本。解析器要求内存保留表、结构区、字符串区按此顺序排列。首次访问 `dtb_pa` 依赖可信的启动链，开启分页前需要为 DTB 建立有效映射或复制它。

## v0.1 验收

`make -B -j4` 和 `make -B -j4 TEST=1` 均可完成构建。当前 QEMU `virt`、128 MiB、单 hart 配置下，`make test` 输出 hart 0、`[0x80000000, 0x88000000)` 的 RAM，以及两段 OpenSBI 保留区；四组自测合计 `143/143 passed, all OK`。

内核自测失败仍只会打印在控制台；`make test` 的 Python 集成脚本会检查自测汇总、QEMU 退出码和超时，因此可用命令退出码判定整轮测试是否成功。
