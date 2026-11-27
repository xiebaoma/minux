# Minux

Minux 是一个面向 RISC-V 64 的类 Unix 宏内核。它参照 Linux 的内核架构，使用 Linux RISC-V 系统调用 ABI 的一个子集运行自带的用户态程序。

**v1.1 支持按架构选择构建配置。** 当前支持 RISC-V 64，通过 OpenSBI 在 QEMU `virt` 上以单 hart 启动。内核提供 Sv39 虚拟内存、抢占式调度、用户进程和 VFS，用户环境包含串口 TTY、最小 libc、shell、`echo` 和 `cat`。

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

```bash
git clone --branch main https://github.com/xiebaoma/minux.git
cd minux
make run
```

出现 `minux$ ` 提示符后，可以输入下面两行。第二行应输出 `hello Minux`。

```text
/bin/echo hello Minux | /bin/cat > /tmp/hello
/bin/cat < /tmp/hello
```

shell 通过绝对路径执行前台命令，支持一条连接两个命令的管道，以及 `<` 和 `>` 重定向。`/tmp` 中的文件只保留到本次运行结束，`/disk` 是只读 ext2。按 `Ctrl-A` 后再按 `x` 退出 QEMU。

| 命令 | 说明 |
|---|---|
| `make` | 构建内核 `build/minux.elf` 和只读 ext2 镜像 `build/disk.ext2` |
| `make run` | 构建并启动正式镜像 |
| `make test` | 构建自测镜像并运行完整回归测试 |
| `make debug` | 启动 QEMU 并在第一条指令前暂停，GDB 服务监听 `localhost:1234` |
| `make dump` | 生成反汇编文件 `build/minux.asm` |
| `make clean` | 清理构建产物 |

`make run` 默认选择 RISC-V，等价于 `make ARCH=riscv run`。工具链、链接脚本、QEMU 机型和汇编入口由 [RISC-V 构建配置](arch/riscv/Makefile) 选择。可用 `CROSS_COMPILE` 覆盖交叉工具链前缀。正式镜像在 `build/`，自测镜像在 `build/test/`。

## 测试

`make test` 先检查默认和显式架构选择、测试对象隔离及未知架构报错，再用同一个自测内核分别启动两张 ext2 测试盘。两轮都应报告 `537/537 passed, all OK`，并分别回收磁盘子程序的退出码 42 和 73。集成测试还会检查串口行编辑、Ctrl-D、前台进程、管道、文件重定向和错误恢复。命令返回 0 表示完整回归通过。

RISC-V 配置从 `arch/riscv/tests/` 选择 Sv39 测试和四个汇编探针。内核依次执行启动自测、rootfs 自测和开中断后的运行时自测。v1.1 两轮的 22 组测试均与 v1.0 的执行顺序和检查数一致。[v1.0 基线](docs/BASELINE-v1.0.md)

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
user/                  /init、hello、echo、cat、sh（启动归档）和 /disk/bin/child（ext2）
  libc/                用户态头文件、RISC-V 启动入口和系统调用封装
tests/                 内核自测和 QEMU 串口集成测试
docs/                  文件系统、内存管理、调度、FDT、异常与中断设计文档及路线图
```

## 设计文档与路线图

- [文件系统](docs/FS.md)
- [内存管理](docs/MM.md)
- [内核线程与调度](docs/SCHED.md)
- [设备树与早期启动](docs/FDT.md)
- [异常、系统调用与中断](docs/TRAP.md)
- [v1.0 路线图](docs/ROADMAP.md) · [v2.0 开发路线图](docs/ROADMAP-v2.md)
