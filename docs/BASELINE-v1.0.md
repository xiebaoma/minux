# Minux v1.0 回归基线

2026-11-27，在 v1.1 开发前验证了提交 `db06ab4`。该提交与 v1.0 标签（`2d9415c`）之间仅有文档差异。使用该提交的干净源码副本构建，工具链为 riscv64-elf-gcc 16.2.0，运行平台为 QEMU 11.1.3 的 RISC-V `virt`，单 hart、128 MiB RAM。

| 运行 | 内核自测 | `/init` 回收 `/disk/bin/child` 的状态 | 串口结果 |
|---|---|---|---|
| 正式镜像，`make run` | 正式构建关闭自测 | 42 | `/init` 执行 `/bin/sh`，出现 `minux$` 提示符 |
| 自测镜像，`disk.ext2` | 537/537 通过 | 42 | 全部串口场景通过 |
| 同一自测镜像，`disk-alt.ext2` | 537/537 通过 | 73 | 全部串口场景通过 |

两轮均验证了 UART 接收中断、输入回显、退格编辑、Ctrl-D、前台命令、管道、输入输出重定向、文件截断和错误恢复。两轮均观察到串口交互后的 `timer: 20 ticks`，完整 `make test` 返回 0。

## 复现

1. 从上述提交的干净源码目录执行 `make run`，等待串口出现 `minux$`，再按 Ctrl-A、x 退出 QEMU。
2. 执行 `make test`。核对两轮串口各有一条 `537/537 passed, all OK`，并按启动顺序核对 `init: reaped disk child, status 42` 和 `init: reaped disk child, status 73`。
