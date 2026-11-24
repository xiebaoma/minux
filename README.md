# Minux

Minux 是一个面向 RISC-V 64 的类 Unix 宏内核。它以 Linux 的内核架构为参照，在保持代码精简、可读的前提下，提供与 Linux 兼容的系统调用接口。

> **v0.7 开发中，已完成只读根文件系统。** 内核在 QEMU `virt` 单 hart 上挂载内嵌的 initramfs，通过 VFS 加载用户程序 `/init`。`/init` 通过 `clone` 派生子进程，子进程触发写时复制（COW）并执行归档中的 `/bin/child`。父进程通过 `wait4` 核对退出状态。

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

普通 `make run` 的串口日志包含以下四行，时钟日志可能穿插其间。子进程返回 42，父进程核对退出状态和 COW 隔离后返回 0，内核继续输出时钟计数。

```text
user task 1: start /init
user task 2: exec /bin/child
user task 2: exit 42
user task 1: exit 0
```

## 目录结构

```
arch/riscv/            架构相关代码
  head.S               内核入口：建立启动栈、清零 BSS
  kernel.ld            链接脚本
  sbi.c                SBI 调用封装
  include/asm/         架构相关头文件
include/minux/         内核通用头文件（types.h、string.h ...）
fs/                    VFS 路径查找、只读 ramfs 和 newc 归档解析
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
