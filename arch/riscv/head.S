/*
 * 内核入口：OpenSBI 以 S 模式跳到这里，此时
 *   a0 = 当前 hart（CPU 核）编号
 *   a1 = 设备树（DTB）的物理地址
 * 下面的代码不会改动 a0/a1，它们会原样作为 start_kernel 的两个参数。
 */
    .section .text.entry
    .globl _start
_start:
    /* 1. 设置栈指针，之后才能调用 C 函数 */
    la      sp, boot_stack_top

    /* 2. 清零 BSS（C 语言假定未初始化的全局变量为 0） */
    la      t0, __bss_start
    la      t1, __bss_end
1:
    bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:
    /* 3. 进入 C 代码：start_kernel(hartid, dtb) */
    call    start_kernel

    /* start_kernel 不应返回；万一返回了就停在这里 */
3:
    wfi
    j       3b

    .section .bss.stack
    .align  12
boot_stack:
    .space  4096 * 4            /* 16 KiB 启动栈 */
    .globl  boot_stack_top
boot_stack_top:
