# Minux

Minux 是一个面向 RISC-V 64 的类 Unix 宏内核。它以 Linux 的内核架构为参照，在保持代码精简、可读的前提下，提供与 Linux 兼容的系统调用接口。

> **v0.7 支持从只读 ext2 磁盘执行用户程序。** 在 QEMU `virt` 上，内核将磁盘挂到 `/disk`。来自 initramfs 的 `/init` 派生子进程，执行仅在磁盘上的 `/disk/bin/child`。用同一个内核启动两张测试盘，父进程分别回收到退出码 42 和 73。

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

## 文件系统接口

每进程有 16 个 fd 槽，`openat` 分配最低可用编号。支持普通文件的 `O_RDONLY`、`O_WRONLY`、`O_RDWR`、`O_CREAT`、`O_EXCL` 和可写打开时的 `O_TRUNC`。绝对路径忽略 `dirfd`，相对路径仅支持 `AT_FDCWD`，当前工作目录固定为 `/`。创建时接受 `mode` 参数，权限检查留待后续版本。

普通文件的 `read` 和 `write` 按实际传输量推进偏移。独立打开各有自己的位置，fork 继承的 fd 共享位置，exec 保留 fd，close 和进程退出释放引用。归档文件首次写入时复制到可写内存，重启后恢复归档内容。

根文件系统挂载时注册只写设备 `/dev/console`，内核为初始 `/init` 打开它作为标准输出（fd 1）。上例中的 `minux: hello through pipe` 是子进程写入匿名管道、父进程从管道读回后通过 `write(1, …)` 输出的内容。

`pipe2(fds, 0)` 返回读端和写端。每条管道有 4096 字节的环形缓冲区，读空、写满时通过等待队列睡眠。最多 4096 字节的单次写入会等待足够空间。最后一个写端关闭后，读端先读完剩余数据，再返回 0；最后一个读端关闭后，写入返回 `-EPIPE`。当前仅支持阻塞模式。

## 只读 ext2 与磁盘换盘验收

`make` 调用宿主机的 `mke2fs`、`debugfs` 和 `e2fsck` 制作并校验 raw 镜像。脚本会查找常用的 Linux 和 Homebrew 安装目录。镜像固定为 2 MiB，块大小为 1 KiB，含 128 个 256 字节的 inode，并固定 UUID、时间戳和文件系统特性。Minux 内核通过自己的 ext2 实现和块缓存读取镜像。

`/disk` 支持多级目录、普通文件、12 个直接块和一级间接块。在 1 KiB 块的镜像上，单文件最多支持 268 KiB。测试文件 `/disk/share/probe/pattern` 跨过直接块与间接块的边界。创建和写打开均返回 `-EROFS`，需要写入的用户数据仍放在 ramfs 的 `/tmp`。

`make test` 只构建一次自测内核。两次启动分别使用 `build/test/disk.ext2` 和 `build/test/disk-alt.ext2`；集成测试从串口核对子进程退出日志，以及父进程从 `wait4` 状态字解出的 42 和 73。可用 `make build/disk-alt.ext2` 和 `make run DISK=build/disk-alt.ext2` 在普通内核上复现第二次启动。

## 只读块设备

`make run` 和 `make debug` 默认以只读方式接入 `build/disk.ext2`，也可用 `DISK=/path/to/disk.ext2` 指定符合上述读取范围的 ext2 镜像。直接启动内核且不附加磁盘时仍能从 ramfs 启动 `/init`，执行磁盘子程序需要挂载含 `/bin/child` 的镜像。

通用层提供直接读 `blk_read` 和缓存读 `blk_read_cached`，参数为起始扇区、扇区数和内核缓冲区。每台设备有 8 个直接映射的扇区缓存槽。整个范围在发出 I/O 前检查。零长度读可从盘尾开始，后端在多扇区读取中途出错时，前面成功读取的数据可能已复制。容量和镜像内容在挂载期间应保持固定。

RISC-V 平台从设备树收集启用的 virtio-mmio 窗口，驱动选取第一块现代 MMIO v2 块设备。队列 0 同时处理一个读请求，轮询完成后才将数据交给调用者。若设备超时，驱动停用该队列并保留 DMA 缓冲区。

`make test` 会分别报告 `block (memory)` 和 `block (virtio-mmio)`。测试盘在 ext2 卷尾之外附加 32 个已知扇区，与内存模拟盘共用容量、内容和越界检查，内存设备另外记录后端读取次数并模拟 I/O 失败，以验证缓存命中、替换和重试。

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
docs/                  内存管理、调度、FDT、异常与中断设计文档及路线图
```

## 设计文档与路线图

- [内存管理](docs/MM.md)
- [内核线程与调度](docs/SCHED.md)
- [路线图](docs/ROADMAP.md)
