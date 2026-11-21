    .equ SWITCH_FRAME_SIZE, 112
    .equ SWITCH_RA, 0
    .equ SWITCH_S1, 16

    .text
    .align 2
    .globl arch_init_thread
    .type arch_init_thread, @function
arch_init_thread:
    /* a0 为栈顶，a1 为通用线程入口。初始现场与切换现场布局相同。 */
    mv      t0, a0
    addi    a0, a0, -SWITCH_FRAME_SIZE
    mv      t1, a0
1:
    sd      zero, 0(t1)
    addi    t1, t1, 8
    bne     t1, t0, 1b
    lla     t0, .Lthread_start
    sd      t0, SWITCH_RA(a0)
    sd      a1, SWITCH_S1(a0)
    ret
    .size arch_init_thread, . - arch_init_thread

    .align 2
    .globl arch_switch_to
    .type arch_switch_to, @function
arch_switch_to:
    /* 普通函数调用处切换，保存 ra 和 s0～s11。栈帧保持 16 字节对齐。 */
    addi    sp, sp, -SWITCH_FRAME_SIZE
    sd      ra, SWITCH_RA(sp)
    sd      s0, 8(sp)
    sd      s1, SWITCH_S1(sp)
    sd      s2, 24(sp)
    sd      s3, 32(sp)
    sd      s4, 40(sp)
    sd      s5, 48(sp)
    sd      s6, 56(sp)
    sd      s7, 64(sp)
    sd      s8, 72(sp)
    sd      s9, 80(sp)
    sd      s10, 88(sp)
    sd      s11, 96(sp)
    sd      sp, 0(a0)

    mv      sp, a1
    ld      ra, SWITCH_RA(sp)
    ld      s0, 8(sp)
    ld      s1, SWITCH_S1(sp)
    ld      s2, 24(sp)
    ld      s3, 32(sp)
    ld      s4, 40(sp)
    ld      s5, 48(sp)
    ld      s6, 56(sp)
    ld      s7, 64(sp)
    ld      s8, 72(sp)
    ld      s9, 80(sp)
    ld      s10, 88(sp)
    ld      s11, 96(sp)
    addi    sp, sp, SWITCH_FRAME_SIZE
    ret
    .size arch_switch_to, . - arch_switch_to

.Lthread_start:
    /* 首次运行没有调用者，s0 已清零，ra 也以零结束回溯。 */
    li      ra, 0
    jr      s1
