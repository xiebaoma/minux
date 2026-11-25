# Minux 构建脚本

CROSS_COMPILE ?= riscv64-elf-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
AR      := $(CROSS_COMPILE)ar
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
FREESTANDING_CFLAGS := -std=gnu11 -Wall -Wextra -O2 -g -mabi=lp64 -mcmodel=medany \
           -ffreestanding -fno-builtin -nostdlib \
           -fno-stack-protector -fno-pie -fno-omit-frame-pointer -MMD -MP
CFLAGS := $(FREESTANDING_CFLAGS) -march=rv64imac_zicsr_zifencei -Iinclude -I$(ARCH)/include
USER_CFLAGS := $(FREESTANDING_CFLAGS) -march=rv64imac -nostdinc -Iuser/libc/include
# 分页前按物理地址执行高地址链接的代码，保留 PC 相对寻址。
LDFLAGS := -T $(ARCH)/kernel.ld -nostdlib --no-relax

# 自动收集这些目录下的源文件：新建 .c 文件后无需改 Makefile。
# 链接顺序无所谓：_start 位于 .text.entry 段，链接脚本保证它排在最前。
SRC_DIRS := $(ARCH) init lib kernel mm fs block drivers/virtio
ifeq ($(TEST),1)
SRC_DIRS += tests
CFLAGS   += -DCONFIG_SELFTEST
endif
SRCS := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c $(d)/*.S))
OBJS := $(patsubst %,$(BUILD)/%.o,$(basename $(SRCS)))

QEMU      := qemu-system-riscv64
QEMU_BASE_FLAGS := -machine virt -nographic -bios default -m 128M -smp 1
BLOCK_FIXTURE := $(BUILD)/block-test.img
EXT2_DISK := $(BUILD)/disk.ext2
DISK ?= $(EXT2_DISK)
qemu_disk = -global virtio-mmio.force-legacy=false -drive if=none,id=vd0,format=raw,readonly=on,file="$(1)" -device virtio-blk-device,drive=vd0
QEMUFLAGS := $(QEMU_BASE_FLAGS) $(call qemu_disk,$(DISK)) -kernel $(KERNEL)

.PHONY: all run test debug dump clean

all: $(KERNEL) $(EXT2_DISK)

ifeq ($(TEST),1)
EXT2_FIXTURE := $(BLOCK_FIXTURE)
endif

$(EXT2_DISK): $(BUILD)/user/child.elf tools/mkext2.py $(EXT2_FIXTURE)
	python3 tools/mkext2.py $@ $< $(if $(EXT2_FIXTURE),--sector-fixture $(EXT2_FIXTURE))

$(BUILD)/user/child-alt.o: user/child.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DCHILD_EXIT_CODE=73 -c $< -o $@

$(BUILD)/disk-alt.ext2: $(BUILD)/user/child-alt.elf tools/mkext2.py $(EXT2_FIXTURE)
	python3 tools/mkext2.py $@ $< $(if $(EXT2_FIXTURE),--sector-fixture $(EXT2_FIXTURE))

$(BLOCK_FIXTURE): tools/mkblockimage.py
	@mkdir -p $(dir $@)
	python3 $< $@.tmp
	mv $@.tmp $@

# 构建带自测的内核，在 QEMU 中验证内核自测和 UART/PLIC 中断
test:
	$(MAKE) TEST=1 all build/test/disk-alt.ext2
	python3 tests/test_uart_irq.py 42 $(QEMU) $(QEMU_BASE_FLAGS) \
		$(call qemu_disk,build/test/disk.ext2) -kernel build/test/minux.elf -monitor none -serial stdio
	python3 tests/test_uart_irq.py 73 $(QEMU) $(QEMU_BASE_FLAGS) \
		$(call qemu_disk,build/test/disk-alt.ext2) -kernel build/test/minux.elf -monitor none -serial stdio

$(KERNEL): $(OBJS) $(ARCH)/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# 用户 ELF 独立链接，保留程序头供内核加载，省去内嵌调试信息。
$(BUILD)/user/%.elf: $(BUILD)/user/%.o $(ARCH)/user.ld
	$(LD) -T $(ARCH)/user.ld --no-relax -s -o $@ $<

# 只打包清单中的文件；先写临时文件，避免失败后复用半个归档。
ROOTDIR := $(BUILD)/rootfs
ROOT_FILES := init bin/hello
ROOT_DIRS := bin tmp dev disk
ifeq ($(TEST),1)
ROOT_FILES += test/truncated test/pipes
ROOT_DIRS += test
endif

$(ROOTDIR)/init: $(BUILD)/user/init.elf
	@mkdir -p $(dir $@)
	cp $< $@

USER_LIBDIR := $(BUILD)/user/libc
USER_CRT := $(USER_LIBDIR)/$(ARCH)/crt0.o
USER_SYSCALLS := $(USER_LIBDIR)/$(ARCH)/syscalls.o
HELLO_OBJ := $(BUILD)/user/hello.o

$(HELLO_OBJ) $(USER_CRT) $(USER_SYSCALLS): CFLAGS := $(USER_CFLAGS)
$(USER_SYSCALLS): CPPFLAGS += -I$(ARCH)/include

$(USER_LIBDIR)/libc.a: $(USER_SYSCALLS)
	$(AR) rcs $@ $^

$(ROOTDIR)/bin/hello: $(USER_CRT) $(HELLO_OBJ) $(USER_LIBDIR)/libc.a $(ARCH)/user.ld
	@mkdir -p $(dir $@)
	$(LD) -T $(ARCH)/user.ld --no-relax -s -o $@ $(filter %.o %.a,$^)

$(ROOTDIR)/test/pipes: $(BUILD)/tests/user/pipe.o $(ARCH)/user.ld
	@mkdir -p $(dir $@)
	$(LD) -T $(ARCH)/user.ld --no-relax -s -o $@ $<

$(ROOTDIR)/test/truncated: $(BUILD)/user/init.elf
	@mkdir -p $(dir $@)
	head -c 4 $< > $@

$(BUILD)/initramfs.cpio: $(addprefix $(ROOTDIR)/,$(ROOT_FILES)) Makefile
	@mkdir -p $(addprefix $(ROOTDIR)/,$(ROOT_DIRS))
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

run: $(KERNEL) $(DISK)
	$(QEMU) $(QEMUFLAGS)

# 启动后暂停，等待 GDB 连接 localhost:1234
debug: $(KERNEL) $(DISK)
	$(QEMU) $(QEMUFLAGS) -s -S

dump: $(KERNEL)
	$(OBJDUMP) -d $(KERNEL) > $(BUILD)/minux.asm
	@echo "反汇编已写入 $(BUILD)/minux.asm"

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d) $(BUILD)/user/init.d $(BUILD)/user/child.d $(BUILD)/user/child-alt.d $(BUILD)/tests/user/pipe.d
-include $(HELLO_OBJ:.o=.d) $(USER_CRT:.o=.d) $(USER_SYSCALLS:.o=.d)
.SECONDARY: $(BUILD)/user/init.o $(BUILD)/user/child.o
