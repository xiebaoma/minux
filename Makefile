# Minux 构建脚本

ARCH ?= riscv
.DEFAULT_GOAL := all
ARCH_DIR := arch/$(ARCH)
ARCH_CONFIG := $(ARCH_DIR)/Makefile
ifeq ($(filter $(ARCH_CONFIG),$(wildcard arch/*/Makefile)),)
$(error Unsupported ARCH '$(ARCH)': missing $(ARCH_CONFIG))
endif
include $(ARCH_CONFIG)

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
AR      := $(CROSS_COMPILE)ar
OBJDUMP := $(CROSS_COMPILE)objdump

# TEST=1：把 tests/ 下的自测编进内核（`make test` 会自动设置）。
# 测试版和正式版的目标文件放在不同目录，避免互相混用。
TEST    ?= 0
TEST_BUILD := $(ARCH_BUILD_DIR)/test
ifeq ($(TEST),1)
BUILD   := $(TEST_BUILD)
else
BUILD   := $(ARCH_BUILD_DIR)
endif
KERNEL  := $(BUILD)/minux.elf

# 固定 C 标准为 gnu11（与 Linux 相同）。GCC 15+ 默认 C23，
# 那里 bool/true/false 是关键字，会与 types.h 的定义冲突。
FREESTANDING_CFLAGS := -std=gnu11 -Wall -Wextra -O2 -g $(ARCH_CFLAGS) \
           -ffreestanding -fno-builtin -nostdlib \
           -fno-stack-protector -fno-pie -fno-omit-frame-pointer -MMD -MP
CFLAGS := $(FREESTANDING_CFLAGS) $(ARCH_KERNEL_CFLAGS) -Iinclude -I$(ARCH_DIR)/include
USER_CFLAGS := $(FREESTANDING_CFLAGS) $(ARCH_USER_CFLAGS) -nostdinc -Iuser/libc/include -Iinclude/uapi
LDFLAGS := -T $(KERNEL_LDSCRIPT) -nostdlib $(ARCH_LDFLAGS)
USER_LDFLAGS := -T $(USER_LDSCRIPT) $(ARCH_LDFLAGS) -s

# 自动收集这些目录下的源文件：新建 .c 文件后无需改 Makefile。
# 链接顺序无所谓：_start 位于 .text.entry 段，链接脚本保证它排在最前。
SRC_DIRS := $(ARCH_DIR) init lib kernel mm fs block $(ARCH_DRIVER_DIRS)
SRCS := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c $(d)/*.S))
ifeq ($(TEST),1)
SRCS     += $(wildcard tests/*.c) $(ARCH_TEST_PROBES)
CFLAGS   += -DCONFIG_SELFTEST
endif
OBJS := $(patsubst %,$(BUILD)/%.o,$(basename $(SRCS)))

BLOCK_FIXTURE := $(BUILD)/block-test.img
EXT2_DISK := $(BUILD)/disk.ext2
DISK ?= $(EXT2_DISK)
QEMUFLAGS := $(call qemu_boot,$(KERNEL),$(DISK))
USER_INIT := $(BUILD)/$(ARCH_INIT_SRC:.S=.elf)
DISK_CHILD := $(BUILD)/$(ARCH_CHILD_SRC:.S=.elf)
PIPE_TEST_OBJ := $(BUILD)/$(ARCH_PIPE_TEST_SRC:.c=.o)

.PHONY: all run test debug dump clean

all: $(KERNEL) $(EXT2_DISK)

ifeq ($(TEST),1)
EXT2_FIXTURE := $(BLOCK_FIXTURE)
endif

$(EXT2_DISK): $(DISK_CHILD) tools/mkext2.py $(EXT2_FIXTURE)
	python3 tools/mkext2.py $@ $< $(if $(EXT2_FIXTURE),--sector-fixture $(EXT2_FIXTURE))

$(BUILD)/user/child-alt.o: $(ARCH_CHILD_SRC) Makefile $(ARCH_CONFIG)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCHILD_EXIT_CODE=73 -c $< -o $@

$(BUILD)/disk-alt.ext2: $(BUILD)/user/child-alt.elf tools/mkext2.py $(EXT2_FIXTURE)
	python3 tools/mkext2.py $@ $< $(if $(EXT2_FIXTURE),--sector-fixture $(EXT2_FIXTURE))

$(BLOCK_FIXTURE): tools/mkblockimage.py
	@mkdir -p $(dir $@)
	python3 $< $@.tmp
	mv $@.tmp $@

# 构建带自测的内核，在 QEMU 中验证内核自测和 UART/PLIC 中断
test:
	MAKE="$(MAKE)" python3 tests/test_build_arch.py
	$(MAKE) TEST=1 all $(TEST_BUILD)/disk-alt.ext2
	python3 $(SERIAL_TEST) 42 $(QEMU) $(call qemu_boot,$(TEST_BUILD)/minux.elf,$(TEST_BUILD)/disk.ext2) $(QEMU_TEST_FLAGS)
	python3 $(SERIAL_TEST) 73 $(QEMU) $(call qemu_boot,$(TEST_BUILD)/minux.elf,$(TEST_BUILD)/disk-alt.ext2) $(QEMU_TEST_FLAGS)

$(KERNEL): $(OBJS) $(KERNEL_LDSCRIPT)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# 用户 ELF 独立链接，保留程序头供内核加载，省去内嵌调试信息。
$(BUILD)/%.elf: $(BUILD)/%.o $(USER_LDSCRIPT)
	$(LD) $(USER_LDFLAGS) -o $@ $<

# 只打包清单中的文件；先写临时文件，避免失败后复用半个归档。
ROOTDIR := $(BUILD)/rootfs
USER_COMMANDS := hello echo cat sh
ROOT_FILES := init $(addprefix bin/,$(USER_COMMANDS))
ROOT_DIRS := bin tmp dev disk
ifeq ($(TEST),1)
ROOT_FILES += test/truncated test/pipes test/commands
ROOT_DIRS += test
endif

$(ROOTDIR)/init: $(USER_INIT)
	@mkdir -p $(dir $@)
	cp $< $@

USER_LIBDIR := $(BUILD)/user/libc
USER_CRT := $(BUILD)/$(ARCH_CRT_SRC:.S=.o)
USER_SYSCALLS := $(BUILD)/$(ARCH_SYSCALLS_SRC:.S=.o)
USER_OBJS := $(addprefix $(BUILD)/user/,$(addsuffix .o,$(USER_COMMANDS)))
LIBC_OBJS := $(USER_SYSCALLS) $(USER_LIBDIR)/string.o

$(USER_OBJS) $(USER_CRT) $(LIBC_OBJS) $(BUILD)/tests/user/commands.o: CFLAGS := $(USER_CFLAGS)
$(USER_SYSCALLS): CPPFLAGS += -I$(ARCH_DIR)/include

$(USER_LIBDIR)/libc.a: $(LIBC_OBJS)
	$(AR) rcs $@ $^

$(ROOTDIR)/bin/%: $(BUILD)/user/%.o $(USER_CRT) $(USER_LIBDIR)/libc.a $(USER_LDSCRIPT)
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT) $< $(USER_LIBDIR)/libc.a

$(ROOTDIR)/test/commands: $(BUILD)/tests/user/commands.o $(USER_CRT) $(USER_LIBDIR)/libc.a $(USER_LDSCRIPT)
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT) $< $(USER_LIBDIR)/libc.a

$(ROOTDIR)/test/pipes: $(PIPE_TEST_OBJ) $(USER_LDSCRIPT)
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $<

$(ROOTDIR)/test/truncated: $(USER_INIT)
	@mkdir -p $(dir $@)
	head -c 4 $< > $@

$(BUILD)/initramfs.cpio: $(addprefix $(ROOTDIR)/,$(ROOT_FILES)) Makefile
	@mkdir -p $(addprefix $(ROOTDIR)/,$(ROOT_DIRS))
	cd $(ROOTDIR) && printf '%s\n' $(ROOT_DIRS) $(ROOT_FILES) | cpio -o -H newc > ../initramfs.cpio.tmp
	mv $@.tmp $@

$(BUILD)/init/images.o: $(BUILD)/initramfs.cpio
$(BUILD)/init/images.o: CPPFLAGS += -DINITRAMFS='"$(BUILD)/initramfs.cpio"'

$(BUILD)/%.o: %.c Makefile $(ARCH_CONFIG)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S Makefile $(ARCH_CONFIG)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

run: $(KERNEL) $(DISK)
	$(QEMU) $(QEMUFLAGS)

# 启动后暂停，等待 GDB 连接 localhost:1234
debug: $(KERNEL) $(DISK)
	$(QEMU) $(QEMUFLAGS) $(QEMU_DEBUG_FLAGS)

dump: $(KERNEL)
	$(OBJDUMP) -d $(KERNEL) > $(BUILD)/minux.asm
	@echo "反汇编已写入 $(BUILD)/minux.asm"

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d) $(USER_INIT:.elf=.d) $(DISK_CHILD:.elf=.d) $(BUILD)/user/child-alt.d $(PIPE_TEST_OBJ:.o=.d)
-include $(USER_OBJS:.o=.d) $(USER_CRT:.o=.d) $(LIBC_OBJS:.o=.d) $(BUILD)/tests/user/commands.d
.SECONDARY: $(USER_INIT:.elf=.o) $(DISK_CHILD:.elf=.o)
