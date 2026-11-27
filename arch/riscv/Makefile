# 单核 RV64，QEMU virt，OpenSBI。沿用 v1.0 的产物路径。
ARCH_BUILD_DIR := build
CROSS_COMPILE ?= riscv64-elf-

ARCH_CFLAGS := -mabi=lp64 -mcmodel=medany
ARCH_KERNEL_CFLAGS := -march=rv64imac_zicsr_zifencei
ARCH_USER_CFLAGS := -march=rv64imac
ARCH_LDFLAGS := --no-relax
KERNEL_LDSCRIPT := $(ARCH_DIR)/kernel.ld
USER_LDSCRIPT := $(ARCH_DIR)/user.ld
ARCH_DRIVER_DIRS := drivers/virtio

ARCH_CRT_SRC := user/libc/arch/riscv/crt0.S
ARCH_SYSCALLS_SRC := user/libc/arch/riscv/syscalls.S
ARCH_INIT_SRC := user/init.S
ARCH_CHILD_SRC := user/child.S

# Sv39 自测与探针只链接到 RISC-V 自测内核。
ARCH_TEST_SRCS := $(wildcard $(ARCH_DIR)/tests/*.c $(ARCH_DIR)/tests/*.S)
ARCH_PIPE_TEST_SRC := tests/user/pipe.c
SERIAL_TEST := tests/test_uart_irq.py

QEMU := qemu-system-riscv64
QEMU_BASE_FLAGS := -machine virt -nographic -bios default -m 128M -smp 1
QEMU_TEST_FLAGS := -monitor none -serial stdio
QEMU_DEBUG_FLAGS := -s -S
# 参数依次为内核 ELF 和只读启动盘。
qemu_boot = $(QEMU_BASE_FLAGS) -global virtio-mmio.force-legacy=false \
            -drive if=none,id=vd0,format=raw,readonly=on,file="$(2)" \
            -device virtio-blk-device,drive=vd0 -kernel $(1)
