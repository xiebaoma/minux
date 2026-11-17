# Trap、定时器与外部中断（v0.2）

## 启动与处理流程

`start_kernel()` 首先调用 `trap_init()`，将 `stvec` 指向统一的汇编入口。RISC-V 发生 trap 时，硬件将返回地址、状态和原因写入 `sepc`、`sstatus`、`scause`、`stval` 等 CSR，然后跳转到该入口。汇编在当前内核栈上建立 `pt_regs`，保存全部通用寄存器与这四个 CSR，再调用 C 处理函数。

未处理的异常和中断会打印 `scause` 编号、`sepc`、`stval` 与 `sstatus`，随后停机。定时器中断由 `scause=5` 分发，外部中断由 `scause=9` 分发；处理后从 `pt_regs` 恢复现场并执行 `sret`，回到原执行位置。处理器不会自动保存通用寄存器；这一步必须由汇编完成。

## 数据结构与取舍

`pt_regs.x[0..31]` 按寄存器编号存放，其中 `x[0]` 始终为零，`x[2]` 是进入 trap 前的栈指针。其后依次是 `epc`、`status`、`cause`、`tval`。固定偏移由汇编和 C 共用，C 侧用编译期断言检查布局。保存区为 288 字节，保持 16 字节栈对齐。

当前仅从 S 模式内核进入 trap，因此直接使用现有内核栈。v0.6 引入用户态之前，入口需要增加从用户栈切换到内核栈的流程。这里的 `pt_regs` 和 Linux RISC-V 的同名结构承担相同职责；字段顺序暂按 Minux 的简单寄存器编号布局组织。

## 周期性定时器

启动代码从 DTB 的 `/cpus/timebase-frequency` 取得 `time` 计数器频率。`timer_init()` 按该频率计算每秒 10 次的间隔，通过 SBI TIME 扩展设置首次绝对截止时间，然后分别打开 `sie.STIE` 和 `sstatus.SIE`。每次定时器中断都重新设置下次截止时间并增加 tick 计数。普通内核上下文每秒输出一次计数；自测镜像观察到两次输出后关机。

SBI TIME 的参数是绝对时间，且设置未来的截止时间会清除待处理的定时器中断，见 [SBI TIME 规范](https://docs.riscv.org/reference/sbi/v3.0/ext-time.html)。当前采用“处理时刻 + 间隔”，因此处理耗时会带来少量周期漂移；此阶段先验证稳定触发，后续可改为基于上一次截止时间累计。

## PLIC 与 UART 接收

`virt_probe_irq_devices()` 再次遍历 DTB，读取 `/soc` 下的 PLIC 和 `ns16550a` UART 地址、UART 中断号以及 PLIC 的 S 模式上下文。当前只支持 QEMU `virt` 的单 hart 配置：`/soc/ranges` 必须为空，PLIC 的 `interrupts-extended` 每项按一个 phandle cell 加一个中断 cell 解析。探测时还会确认 UART 的 `interrupt-parent` 指向该 PLIC。当前 QEMU 配置得到 PLIC `0x0c000000`、S 上下文 1、UART `0x10000000`、IRQ 10。

PLIC 为 UART IRQ 设置正优先级、打开 S 上下文的 enable 位，并把阈值设为 0。UART 保留 OpenSBI 配置的波特率，开启接收 FIFO 与接收数据中断；最后打开 `sie.SEIE`。收到字符时，UART 向 PLIC 请求中断，CPU 进入 `scause=9` 的 trap。处理代码从 PLIC 认领 IRQ，读空 UART 接收 FIFO 并逐字节回显，再向 PLIC 写回同一 IRQ 号表示完成。PLIC 寄存器布局和认领流程见 [PLIC 规范](https://docs.riscv.org/reference/hardware/plic/_attachments/riscv-plic.pdf)。

此实现只处理接收中断，回显时轮询发送寄存器。日志输出仍经 SBI 早期控制台；输入缓冲、TTY 行规程和并发控制留给后续里程碑。

## 本阶段验证

- 正常镜像与自测镜像均成功构建；QEMU 自测结果为 `143/143 passed, all OK`。
- 用未加入仓库的临时镜像执行非法指令，QEMU 输出 `unhandled exception: cause=2` 和故障地址，验证了 trap 入口及异常诊断路径。
- QEMU 自测镜像随后输出 `timer: 10 ticks`、`timer: 20 ticks` 并正常关机，验证了重复设置定时器和中断返回路径。
- 使用独立串口 socket 分两次向 QEMU 输入 `IRQ-one` 和 `IRQ-two`，两段文本各回显一次，随后仍能收到 `timer: 20 ticks` 并正常关机。这验证了 UART 中断可以重复处理，且定时器中断可以接续工作。
