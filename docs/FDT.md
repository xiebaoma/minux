# FDT 早期读取（v0.1）

## 目的与启动顺序

在当前 QEMU `virt` 启动路径中，OpenSBI 通过 `a0` 传入启动 hart 编号、通过 `a1` 传入 DTB 的物理地址。Minux 尚未启用分页：`head.S` 使用内核镜像中预留的启动栈，再调用 `start_kernel(hartid, dtb_pa)`。此时读取 DTB 不依赖物理内存分配器；启动链必须保证传入地址可读，并在解析期间保留 DTB 内容。

`start_kernel()` 的顺序是：检查 DTB 头部，遍历节点和属性，输出 RAM 与 `/reserved-memory` 的静态范围，再遍历头部指向的内存保留表。它还会输出内核镜像和 DTB 自身的地址区间。所有区间均使用 `[起点, 终点)` 表示，终点不属于该区间。

## 数据如何读取

DTB 是一块连续的扁平字节数据，不是已经展开的 C 节点树。头部包含总长度，以及内存保留表、结构区和属性名字符串区的偏移。结构区以 `FDT_BEGIN_NODE`、`FDT_PROP`、`FDT_END_NODE` 等 token 表示层级；属性名通过字符串区偏移取得，属性值仍指向原 DTB 内的字节。

| 接口 | 当前用途 |
|---|---|
| `fdt_check_header()` | 检查 magic、版本、区块偏移、长度与对齐。 |
| `fdt_walk()` | 顺序遍历结构区，通过回调交出节点和属性。 |
| `fdt_read_u32()` / `fdt_read_cells()` | 按大端读取一个或两个 32 位 cell。 |
| `fdt_walk_reservations()` | 遍历由 64 位地址、64 位长度组成的内存保留表。 |

`reg` 中每组地址和长度各占多少 cell，由**该节点的父节点**的 `#address-cells` 和 `#size-cells` 决定。根节点缺少它们时，按规范默认使用 2 和 1。当前 QEMU `virt` 根节点使用 2 和 2；例如内存节点的 `reg = <0 0x80000000 0 0x08000000>` 表示 `[0x80000000, 0x88000000)`，即 128 MiB。

内存保留表与 `/reserved-memory` 是两处不同的数据来源。前者在 DTB 头部指定的独立区块中；后者是树中的节点。当前 QEMU/OpenSBI 把两段 M 模式固件占用区放在 `/reserved-memory` 下。RAM 节点描述的是物理 RAM 总范围，包含其中被固件和内核占用的地址；不能直接当作空闲页列表。

## v0.1 支持范围

此实现面向当前 QEMU `virt` 和 OpenSBI 提供的 DTB，读取根节点下 `memory` / `memory@...` 的 `reg`，以及 `/reserved-memory` 直接子节点的静态 `reg`。地址和长度分别支持 1 或 2 个 cell；非空的 `/reserved-memory/ranges` 需要地址转换，当前会报错停机。解析器要求内存保留表、结构区、字符串区按此顺序排列。

已知限制：

- `/reserved-memory` 子节点的 `size` 动态分配请求目前不会被处理；v0.3 引入物理内存分配器时需要实现或明确拒绝。
- 目前未检查节点的 `status`。带 `status = "disabled"` 的静态保留区仍会被打印和计数；扩大平台支持前需修正。
- `no-map`、`reusable` 等属性尚未转化为映射或分配策略。结构遍历也未实现设备树规范的全部语法校验。
- 头部检查只能检查已经读到的内容。首次访问 `dtb_pa` 依赖可信的启动链；当前没有异常处理器来恢复错误的物理地址访问。

Minux 目前只在遍历时打印范围，没有把解析结果保存成节点树或可分配内存表。后续建立分配器时，需在 RAM 总范围中排除固件保留区、内核镜像、DTB 和早期分配所占空间；开启分页前还需为 DTB 建立有效映射或复制它。Linux 的早期启动也先读取 FDT，再建立用于管理物理页的内存区域信息；其临时页表与 FDT 映射流程见 [RISC-V 启动文档](https://www.kernel.org/doc/html/latest/arch/riscv/boot.html)。

## v0.1 验收

`make -B -j4` 和 `make -B -j4 TEST=1` 均可完成构建。当前 QEMU `virt`、128 MiB、单 hart 配置下，`make test` 输出 hart 0、`[0x80000000, 0x88000000)` 的 RAM，以及两段 OpenSBI 保留区；四组自测合计 `143/143 passed, all OK`。

内核自测失败仍只会打印在控制台；`make test` 的 Python 集成脚本会检查自测汇总、QEMU 退出码和超时，因此可用命令退出码判定整轮测试是否成功。
