# Minux

Minux 是一个面向 RISC-V 64 的类 Unix 宏内核。它以 Linux 的内核架构为参照，在保持代码精简、可读的前提下，提供与 Linux 兼容的系统调用接口。

> **v0.7 开发中，已完成可写 ramfs、设备输出和匿名管道。** 在 QEMU `virt` 上，`/init` 在 `/tmp/fd-demo` 写入 `abc`，关闭后独立打开两次，两次都读到 `a`。随后它派生子进程并执行 `/bin/child`，子进程用继承的 fd 读到 `b`，父进程在 `wait4` 返回后读到 `c`。

## 设计目标

- **平台**：RISC-V 64（RV64IMAC），首要目标机器为 QEMU `virt`
- **语言**：C（GNU C11），只在必要处使用汇编
- **架构**：宏内核。子系统划分和核心数据结构（`task_struct`、VFS、buddy 分配器等）参照 Linux
- **ABI**：兼容 Linux RISC-V 系统调用接口，目标是直接运行 musl 静态链接的用户程序

## 构建依赖

| 组件 | 已验证版本 |
|---|---|
| RISC-V 交叉编译工具链（GCC + binutils） | GCC 16.2.0 / binutils 2.47 |
| QEMU（`qemu-system-riscv64`） | 11.1.3 |
| cpio | 支持 `-o -H newc`（macOS 系统自带） |
| GNU Make | 3.81 及以上 |
| Python 3（`make test` 使用标准库） | 3.9 及以上 |

macOS（Homebrew）：

```bash
brew install qemu riscv64-elf-gcc python
```

## 构建与运行

| 命令 | 说明 |
|---|---|
| `make` | 构建内核镜像 `build/minux.elf` |
| `make run` | 在 QEMU 中启动（按 `Ctrl-A X` 退出） |
| `make test` | 在 QEMU 中运行调度、内存等内核自测，自动核对 `/init` 父子的退出结果、UART/PLIC 回显和正常关机 |
| `make debug` | 启动 QEMU 并在第一条指令前暂停，GDB 服务监听 `localhost:1234` |
| `make dump` | 生成反汇编文件 `build/minux.asm` |
| `make clean` | 清理构建产物 |

普通 `make run` 的串口日志包含以下五行，时钟日志可能穿插其间。子进程返回 42，父进程核对退出状态和 COW 隔离后返回 0，内核继续输出时钟计数。

```text
user task 1: start /init
user task 2: exec /bin/child
user task 2: exit 42
minux: hello through pipe
user task 1: exit 0
```

## 文件系统接口

每进程有 16 个 fd 槽，`openat` 分配最低可用编号。支持普通文件的 `O_RDONLY`、`O_WRONLY`、`O_RDWR`、`O_CREAT`、`O_EXCL` 和可写打开时的 `O_TRUNC`。绝对路径忽略 `dirfd`，相对路径仅支持 `AT_FDCWD`，当前工作目录固定为 `/`。创建时接受 `mode` 参数，权限检查留待后续版本。

普通文件的 `read` 和 `write` 按实际传输量推进偏移。独立打开各有自己的位置，fork 继承的 fd 共享位置，exec 保留 fd，close 和进程退出释放引用。归档文件首次写入时复制到可写内存，重启后恢复归档内容。

根文件系统挂载时注册只写设备 `/dev/console`，内核为初始 `/init` 打开它作为标准输出（fd 1）。上例中的 `minux: hello through pipe` 是子进程写入匿名管道、父进程从管道读回后通过 `write(1, …)` 输出的内容。

`pipe2(fds, 0)` 返回读端和写端。每条管道有 4096 字节的环形缓冲区，读空、写满时通过等待队列睡眠。最多 4096 字节的单次写入会等待足够空间。最后一个写端关闭后，读端先读完剩余数据，再返回 0；最后一个读端关闭后，写入返回 `-EPIPE`。当前仅支持阻塞模式。

## 目录结构

```
arch/riscv/            架构相关代码
  head.S               内核入口：建立启动栈、清零 BSS
  kernel.ld            链接脚本
  sbi.c                SBI 调用封装
  include/asm/         架构相关头文件
include/minux/         内核通用头文件（types.h、string.h ...）
fs/                    VFS 路径查找、fd 表、可写 ramfs、newc 归档解析、控制台设备和管道
init/                  通用初始化（start_kernel）
kernel/                核心子系统（printk、panic、任务与调度）
lib/                   通用库函数（string、vsprintf）
mm/                    启动内存表、buddy、slab、虚拟地址空间管理
user/                  打包进启动归档的用户程序 /init 和 /bin/child
tests/                 内核自测和 QEMU 串口集成测试
docs/                  内存管理、调度、FDT、异常与中断设计文档及路线图
```

## 设计文档与路线图

- [内存管理](docs/MM.md)
- [内核线程与调度](docs/SCHED.md)
- [路线图](docs/ROADMAP.md)
