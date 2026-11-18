# Minux

Minux 是一个面向 RISC-V 64 的类 Unix 宏内核。它以 Linux 的内核架构为参照，在保持代码精简、可读的前提下，提供与 Linux 兼容的系统调用接口。

> **当前版本 `v0.3`**。Minux 在 QEMU `virt` 单 hart 配置下支持物理页帧分配和 `kmalloc()`，启动时报告 RAM 总页数、保留页数和空闲页数。周期性定时器与 UART 中断回显也可用。

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
| `make test` | 构建自测内核，自动检查全部自测、UART/PLIC 回显、定时器继续运行和 QEMU 正常退出 |
| `make debug` | 启动 QEMU 并在第一条指令前暂停，GDB 服务监听 `localhost:1234` |
| `make dump` | 生成反汇编文件 `build/minux.asm` |
| `make clean` | 清理构建产物 |


## 目录结构

```
arch/riscv/            架构相关代码
  head.S               内核入口：建立启动栈、清零 BSS
  kernel.ld            链接脚本
  sbi.c                SBI 调用封装
  include/asm/         架构相关头文件
include/minux/         内核通用头文件（types.h、string.h ...）
init/                  通用初始化（start_kernel）
kernel/                核心子系统（printk、panic）
lib/                   通用库函数（string、vsprintf）
mm/                    启动内存表、buddy 页帧分配器、slab 对象分配器
tests/                 内核自测和 QEMU 串口集成测试
docs/                  内存管理、FDT、异常与中断设计文档及路线图
```

## 设计文档与路线图

- [物理内存管理](docs/MM.md)
- [路线图](docs/ROADMAP.md)
