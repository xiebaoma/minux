# Minux

Minux 是一个面向 RISC-V 64 的类 Unix 宏内核。它以 Linux 的内核架构为参照，在保持代码精简、可读的前提下，提供与 Linux 兼容的系统调用接口。

> **v0.7 支持从只读 ext2 磁盘执行用户程序。** 在 QEMU `virt` 上，内核将磁盘挂到 `/disk`。来自 initramfs 的 `/init` 派生子进程，执行仅在磁盘上的 `/disk/bin/child`。用同一个内核启动两张测试盘，父进程分别回收到退出码 42 和 73。

## 设计目标

- **平台**：RISC-V 64（RV64IMAC），首要目标机器为 QEMU `virt`
- **语言**：C（GNU C11），只在必要处使用汇编
- **架构**：宏内核。子系统划分和核心数据结构（`task_struct`、VFS、buddy 分配器等）参照 Linux
- **用户环境**：沿用 Linux RISC-V 系统调用 ABI，从零实现 Minux 的用户态 libc、命令和 shell，按程序实际需要逐步增加接口

## 构建依赖

| 组件 | 已验证版本 |
|---|---|
| RISC-V 交叉编译工具链（GCC + binutils） | GCC 16.2.0 / binutils 2.47 |
| QEMU（`qemu-system-riscv64`） | 11.1.3 |
| cpio | 支持 `-o -H newc`（macOS 系统自带） |
| e2fsprogs（宿主机制作和校验 ext2 镜像） | 1.47.4 |
| GNU Make | 3.81 及以上 |
| Python 3（生成磁盘镜像、运行集成测试） | 3.9 及以上 |

macOS（Homebrew）：

```bash
brew install qemu riscv64-elf-gcc python e2fsprogs
```

## 构建与运行

| 命令 | 说明 |
|---|---|
| `make` | 构建内核 `build/minux.elf` 和只读 ext2 镜像 `build/disk.ext2` |
| `make run` | 在 QEMU 中启动（按 `Ctrl-A X` 退出） |
| `make test` | 用同一内核分别启动两张盘，每次运行全部内核自测，并核对磁盘程序退出值、串口回显和关机 |
| `make debug` | 启动 QEMU 并在第一条指令前暂停，GDB 服务监听 `localhost:1234` |
| `make dump` | 生成反汇编文件 `build/minux.asm` |
| `make clean` | 清理构建产物 |

普通 `make run` 的串口日志包含以下记录，时钟日志可能穿插其间。子进程返回 42，父进程核对退出状态和 COW 隔离后返回 0，内核继续输出时钟计数。

```text
user task 1: start /init
user task 2: exec /disk/bin/child
user task 2: exit 42
init: reaped disk child, status 42
minux: hello through pipe
user task 1: exit 0
```

## 复现 v0.7 换盘验收

`make test` 依次用默认盘和替换盘启动同一个自测内核，预期父进程分别报告 `status 42` 和 `status 73`。两个镜像来自同一个生成脚本，只有磁盘程序的退出码不同。要在普通内核上手动观察，先运行 `make run`，看到父进程退出后按 `Ctrl-A X` 退出 QEMU，再执行以下命令。

```sh
make run DISK=build/disk-alt.ext2
```

Make 会按需生成替换盘。两次启动均使用 `build/minux.elf`，写入 `/tmp` 的内容只在当次启动有效。[文件系统设计与测试范围](docs/FS.md)

## 目录结构

```
arch/riscv/            架构相关代码
  head.S               内核入口：建立启动栈、清零 BSS
  kernel.ld            链接脚本
  sbi.c                SBI 调用封装
  include/asm/         架构相关头文件
include/minux/         内核通用头文件（types.h、string.h ...）
block/                 只读扇区接口和固定容量缓存
drivers/virtio/        virtio-mmio 传输与 virtio-blk 读请求
fs/                    VFS 路径查找、fd 表、可写 ramfs、只读 ext2、newc 归档解析、控制台设备和管道
init/                  通用初始化（start_kernel）
kernel/                核心子系统（printk、panic、任务与调度）
lib/                   通用库函数（string、vsprintf）
mm/                    启动内存表、buddy、slab、虚拟地址空间管理
user/                  用户程序 /init（启动归档）和 /disk/bin/child（ext2）
tests/                 内核自测和 QEMU 串口集成测试
docs/                  文件系统、内存管理、调度、FDT、异常与中断设计文档及路线图
```

## 设计文档与路线图

- [文件系统](docs/FS.md)
- [内存管理](docs/MM.md)
- [内核线程与调度](docs/SCHED.md)
- [路线图](docs/ROADMAP.md)
