# Minux 构建脚本

CROSS_COMPILE ?= riscv64-elf-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJDUMP := $(CROSS_COMPILE)objdump

ARCH    := arch/riscv

# TEST=1：把 tests/ 下的自测编进内核（`make test` 会自动设置）。
# 测试版和正式版的目标文件放在不同目录，避免互相混用。
TEST    ?= 0
ifeq ($(TEST),1)
BUILD   := build/test
else
BUILD   := build
endif
KERNEL  := $(BUILD)/minux.elf

# 内核不使用浮点（rv64imac + lp64），与 Linux 一致
# 固定 C 标准为 gnu11（与 Linux 相同）。GCC 15+ 默认 C23，
# 那里 bool/true/false 是关键字，会与 types.h 的定义冲突。
CFLAGS  := -std=gnu11 -Wall -Wextra -O2 -g \
           -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany \
           -ffreestanding -fno-builtin -nostdlib \
           -fno-stack-protector -fno-pie -fno-omit-frame-pointer \
           -Iinclude -I$(ARCH)/include -MMD -MP
# 分页前按物理地址执行高地址链接的代码，保留 PC 相对寻址。
LDFLAGS := -T $(ARCH)/kernel.ld -nostdlib --no-relax

# 自动收集这些目录下的源文件：新建 .c 文件后无需改 Makefile。
# 链接顺序无所谓：_start 位于 .text.entry 段，链接脚本保证它排在最前。
SRC_DIRS := $(ARCH) init lib kernel mm fs
ifeq ($(TEST),1)
SRC_DIRS += tests
CFLAGS   += -DCONFIG_SELFTEST
endif
SRCS := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c $(d)/*.S))
OBJS := $(patsubst %,$(BUILD)/%.o,$(basename $(SRCS)))

QEMU      := qemu-system-riscv64
QEMU_BASE_FLAGS := -machine virt -nographic -bios default -m 128M -smp 1
QEMUFLAGS := $(QEMU_BASE_FLAGS) -kernel $(KERNEL)

.PHONY: all run test debug dump clean

all: $(KERNEL)

# 构建带自测的内核，在 QEMU 中验证内核自测和 UART/PLIC 中断
test:
	$(MAKE) TEST=1 all
	python3 tests/test_uart_irq.py $(QEMU) $(QEMU_BASE_FLAGS) \
		-kernel build/test/minux.elf -monitor none -serial stdio

$(KERNEL): $(OBJS) $(ARCH)/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# 用户 ELF 独立链接，保留程序头供内核加载，省去内嵌调试信息。
$(BUILD)/user/%.elf: $(BUILD)/user/%.o $(ARCH)/user.ld
	$(LD) -T $(ARCH)/user.ld --no-relax -s -o $@ $<

# 只打包清单中的文件；先写临时文件，避免失败后复用半个归档。
ROOTDIR := $(BUILD)/rootfs
ROOT_FILES := init bin/child
ROOT_DIRS := bin
ifeq ($(TEST),1)
ROOT_FILES += test/truncated
ROOT_DIRS += test
endif

$(ROOTDIR)/init: $(BUILD)/user/init.elf
	@mkdir -p $(dir $@)
	cp $< $@

$(ROOTDIR)/bin/child: $(BUILD)/user/child.elf
	@mkdir -p $(dir $@)
	cp $< $@

$(ROOTDIR)/test/truncated: $(BUILD)/user/init.elf
	@mkdir -p $(dir $@)
	head -c 4 $< > $@

$(BUILD)/initramfs.cpio: $(addprefix $(ROOTDIR)/,$(ROOT_FILES)) Makefile
	cd $(ROOTDIR) && printf '%s\n' $(ROOT_DIRS) $(ROOT_FILES) | cpio -o -H newc > ../initramfs.cpio.tmp
	mv $@.tmp $@

$(BUILD)/init/images.o: $(BUILD)/initramfs.cpio
$(BUILD)/init/images.o: CPPFLAGS += -DINITRAMFS='"$(BUILD)/initramfs.cpio"'

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

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

-include $(OBJS:.o=.d) $(BUILD)/user/init.d $(BUILD)/user/child.d
.SECONDARY: $(BUILD)/user/init.o $(BUILD)/user/child.o
