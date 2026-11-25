    .text
    .globl _start
    .type _start, @function
_start:
    /* exec 已将 sp 对齐到 16 字节。栈上依次是 argc、argv[]、NULL、envp[]。 */
    ld      a0, 0(sp)
    addi    a1, sp, 8
    slli    t0, a0, 3
    add     a2, a1, t0
    addi    a2, a2, 8
    call    main
    tail    _exit               /* a0 已是 main 的返回值。 */
    .size _start, . - _start
