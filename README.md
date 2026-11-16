# Minux

Minux 是一个面向 RISC-V 64 的类 Unix 宏内核。它以 Linux 的内核架构为参照，在保持代码精简、可读的前提下，提供与 Linux 兼容的系统调用接口。

> **状态**：早期开发。当前版本 `v0.0` 能完成引导，进入 S 模式内核入口。

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

macOS（Homebrew）：

```bash
brew install qemu riscv64-elf-gcc
```

使用其他工具链时，通过 `CROSS_COMPILE` 指定前缀，例如：

```bash
make CROSS_COMPILE=riscv64-unknown-elf-
```

## 构建与运行

| 命令 | 说明 |
|---|---|
| `make` | 构建内核镜像 `build/minux.elf` |
| `make run` | 在 QEMU 中启动（按 `Ctrl-A X` 退出） |
| `make test` | 构建带启动自测的内核 `build/test/minux.elf` 并运行 |
| `make debug` | 启动 QEMU 并在第一条指令前暂停，GDB 服务监听 `localhost:1234` |
| `make dump` | 生成反汇编文件 `build/minux.asm` |
| `make clean` | 清理构建产物 |

调试需要另外安装 RISC-V GDB（例如 `brew install riscv64-elf-gdb`）：

```bash
riscv64-elf-gdb build/minux.elf -ex 'target remote :1234'
```

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
tests/                 内核自测，只在 `make test` 时编译进内核
docs/                  设计文档与路线图
```

## 路线图

见 [docs/ROADMAP.md](docs/ROADMAP.md)。
