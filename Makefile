# Minux 构建脚本

CROSS_COMPILE ?= riscv64-elf-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJDUMP := $(CROSS_COMPILE)objdump

BUILD   := build
KERNEL  := $(BUILD)/minux.elf
ARCH    := arch/riscv

# 内核不使用浮点（rv64imac + lp64），与 Linux 一致
CFLAGS  := -Wall -Wextra -O2 -g \
           -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany \
           -ffreestanding -fno-builtin -nostdlib \
           -fno-stack-protector -fno-pie -fno-omit-frame-pointer \
           -I$(ARCH)/include -MMD -MP
LDFLAGS := -T $(ARCH)/kernel.ld -nostdlib

SRCS := $(ARCH)/head.S \
        $(ARCH)/sbi.c \
        init/main.c
OBJS := $(patsubst %,$(BUILD)/%.o,$(basename $(SRCS)))

QEMU      := qemu-system-riscv64
QEMUFLAGS := -machine virt -nographic -bios default -m 128M -smp 1 \
             -kernel $(KERNEL)

.PHONY: all run debug dump clean

all: $(KERNEL)

$(KERNEL): $(OBJS) $(ARCH)/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(KERNEL)
	$(QEMU) $(QEMUFLAGS)

# 启动后暂停，等待 GDB 连接 localhost:1234
debug: $(KERNEL)
	$(QEMU) $(QEMUFLAGS) -s -S

dump: $(KERNEL)
	$(OBJDUMP) -d $(KERNEL) > $(BUILD)/minux.asm
	@echo "反汇编已写入 $(BUILD)/minux.asm"

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
