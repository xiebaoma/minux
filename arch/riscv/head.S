/*
 * 内核入口：OpenSBI 以 S 模式跳到这里，此时
 *   a0 = 当前 hart（CPU 核）编号
 *   a1 = 设备树（DTB）的物理地址
 * 分页前，PC 相对寻址得到镜像的物理地址。
 */
    .section .text.entry
    .globl _start
_start:
    csrci   sstatus, 2          /* 关闭 S 模式总中断，直到设备初始化完成 */
    /* 1. 设置栈指针，之后才能调用 C 函数 */
    lla     sp, boot_stack_top

    /* 2. 清零 BSS（C 语言假定未初始化的全局变量为 0） */
    lla     t0, __bss_start
    lla     t1, __bss_end
1:
    bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:
    /* 3. a2/a3 传入内核镜像的物理地址范围，a0/a1 沿用固件参数。 */
    lla     a2, _kernel_start
    lla     a3, _kernel_end
    call    riscv_boot_kernel

    /* riscv_boot_kernel 不应返回；万一返回了就停在这里 */
3:
    wfi
    j       3b

    /* a0 = 根页表 PA，a1 = 后续 C 入口的低地址，a2 = 高地址偏移。 */
    .section .text
    .globl riscv_enter_high_half
riscv_enter_high_half:
    lla     t0, .Lhigh_entry
    add     t0, t0, a2
    add     a1, a1, a2
    jr      t0

.Lhigh_entry:
    /* 从高地址重新取栈顶，丢弃低地址上的调用栈。 */
    lla     sp, boot_stack_top
    li      s0, 0
    li      ra, 0
    tail    riscv_paging_finish

    .section .bss.stack
    .align  12
boot_stack:
    .space  4096 * 4            /* 16 KiB 启动栈 */
    .globl  boot_stack_top
boot_stack_top:
