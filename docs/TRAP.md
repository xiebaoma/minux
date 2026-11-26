# Trap、定时器与外部中断

## 启动与处理流程

`start_kernel()` 首先调用 `trap_init()`，将 `stvec` 指向统一的汇编入口。RISC-V 发生 trap 时，硬件将返回地址、状态和原因写入 `sepc`、`sstatus`、`scause`、`stval` 等 CSR，然后跳转到该入口。汇编在任务的内核栈上建立 `pt_regs`，保存全部通用寄存器与这四个 CSR，再调用 C 处理函数。

未处理的内核异常和中断会打印 `scause` 编号、`sepc`、`stval` 与 `sstatus`，随后停机。定时器中断由 `scause=5` 分发，外部中断由 `scause=9` 分发。处理后从 `pt_regs` 恢复现场并执行 `sret`，回到原执行位置。处理器不会自动保存通用寄存器，这一步由汇编完成。

## 数据结构与取舍

`pt_regs.x[0..31]` 按寄存器编号存放，其中 `x[0]` 始终为零，`x[2]` 是进入 trap 前的栈指针。其后依次是 `epc`、`status`、`cause`、`tval`。固定偏移由汇编和 C 共用，C 侧用编译期断言检查布局。保存区为 288 字节，保持 16 字节栈对齐。

v0.6 第一步支持从 U 模式进入 trap。内核执行时 `sscratch` 为零，S 模式 trap 继续使用当前内核栈。返回 U 模式前，汇编把当前任务的内核栈位置写入 `sscratch`。下一次用户 trap 先交换 `sp` 和 `sscratch`，再把用户 `sp` 保存到 `pt_regs`，同时清零 `sscratch`。用户态的 `gp` 和 `tp` 与其他寄存器一同保存，内核的 `gp` 和 `tp` 从用户 trap 现场上方恢复。

用户任务首次运行时使用内核预建的 `pt_regs`，设置用户入口和栈指针，并通过同一条 `sret` 路径进入 U 模式。此时 `sstatus.SPP` 为零。时钟抢占后的返回也使用这条路径。

## 系统调用与用户异常

U 模式的 `ecall` 进入 RISC-V 系统调用分发。Linux RV64 ABI 使用 `a7` 传调用号、`a0` 到 `a5` 传参数、`a0` 接收结果。v0.6 提供 `exit`（93）、`getpid`（172）、`clone`（220）、`execve`（221）和 `wait4`（260）。v0.7 新增 `openat`（56）、`close`（57）、`pipe2`（59）、`read`（63）和 `write`（64）。v0.8 新增 `dup3`（24），供用户程序把打开的文件或管道端复制到指定 fd。未知调用返回 `-ENOSYS`。分发前将 `sepc` 增加 4，使返回用户态后从下一条指令继续执行。`execve` 成功时用新程序的入口、栈指针和清零后的寄存器替换旧现场，并执行 `fence.i`。

取指、读和写缺页分别转成对应的内存访问权限。若内存管理成功补页或完成写时复制，保留 `sepc` 并重试原指令。其余同步用户异常会记录 PID、异常号和地址，将异常映射成退出状态中的信号编号，然后通过 `task_exit_current()` 切走。父进程可用 `wait4` 读取该状态。[内存管理](MM.md) · [退出与等待](SCHED.md)

## 周期性定时器

启动代码从 DTB 的 `/cpus/timebase-frequency` 取得 `time` 计数器频率。`timer_init()` 按该频率计算每秒 10 次的间隔，通过 SBI TIME 扩展设置首次绝对截止时间，然后分别打开 `sie.STIE` 和 `sstatus.SIE`。每次定时器中断都重新设置下次截止时间并增加 tick 计数。自测镜像在 idle 循环中每秒输出一次计数，两次输出后关机。普通镜像的周期性计数输出已关闭，方便串口交互。

从 v0.5 开始，定时器中断更新计数后还会派发就绪线程。被切出的线程重新运行时，从自己内核栈上的 trap 现场恢复。见 [内核线程与调度](SCHED.md)。

SBI TIME 的参数是绝对时间，且设置未来的截止时间会清除待处理的定时器中断，见 [SBI TIME 规范](https://docs.riscv.org/reference/sbi/v3.0/ext-time.html)。当前采用“处理时刻 + 间隔”，因此处理耗时会带来少量周期漂移；此阶段先验证稳定触发，后续可改为基于上一次截止时间累计。

## PLIC 与 UART 接收

`virt_probe_devices()` 再次遍历 DTB，读取 `/soc` 下的 PLIC 和 `ns16550a` UART 地址、UART 中断号以及 PLIC 的 S 模式上下文。当前只支持 QEMU `virt` 的单 hart 配置：`/soc/ranges` 必须为空，PLIC 的 `interrupts-extended` 每项按一个 phandle cell 加一个中断 cell 解析。探测时还会确认 UART 的 `interrupt-parent` 指向该 PLIC。当前 QEMU 配置得到 PLIC `0x0c000000`、S 上下文 1、UART `0x10000000`、IRQ 10。

PLIC 为 UART IRQ 设置正优先级、打开 S 上下文的 enable 位，并把阈值设为 0。UART 保留 OpenSBI 配置的波特率，开启接收 FIFO 与接收数据中断；最后打开 `sie.SEIE`。收到字符时，UART 向 PLIC 请求中断，CPU 进入 `scause=9` 的 trap。处理代码从 PLIC 认领 IRQ，读空 UART 接收 FIFO 并逐字节调用 `tty_receive_char()`，再向 PLIC 写回同一 IRQ 号表示完成。PLIC 寄存器布局和认领流程见 [PLIC 规范](https://docs.riscv.org/reference/hardware/plic/_attachments/riscv-plic.pdf)。

通用 TTY 代码负责输入缓冲、行编辑、回显和唤醒读者。回显和日志都通过早期控制台输出。[串口按行输入](FS.md#串口按行输入)

## 验证

- v0.2 的 QEMU 自测记录为 `143/143 passed, all OK`。
- 用未加入仓库的临时镜像执行非法指令，QEMU 输出 `unhandled exception: cause=2` 和故障地址，验证了 trap 入口及异常诊断路径。
- QEMU 自测镜像随后输出 `timer: 10 ticks`、`timer: 20 ticks` 并正常关机，验证了重复设置定时器和中断返回路径。
- `make test` 通过无本地回显的串口管道发送按键，在两次输入之间等待定时器报告。脚本分别核对即时回显、退格序列和用户态 `cat` 输出的修正行，再检查两种 Ctrl-D 行为以及定时器关机。
