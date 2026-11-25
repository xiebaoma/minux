#include <asm/unistd.h>
#include <minux/fcntl.h>
#include <minux/signal.h>

    .macro open_demo flags
    li a0, AT_FDCWD
    la a1, demo_path
    li a2, (\flags)
    li a3, 0600
    li a7, __NR_openat
    ecall
    .endm

    .macro expect_byte fd, value
    mv a0, \fd
    mv a1, sp
    li a2, 1
    li a7, __NR_read
    ecall
    li t0, 1
    bne a0, t0, .Lfail
    lbu t0, 0(sp)
    li t1, \value
    bne t0, t1, .Lfail
    .endm

    .section .rodata
pipe_message: .ascii "minux: hello through pipe\n"
    .equ PIPE_MESSAGE_LEN, . - pipe_message

    .data
status_message: .ascii "init: reaped disk child, status "
status_digits:  .ascii "00\n"
    .equ STATUS_LEN, . - status_message

    .text
    .globl _start
_start:
    ld t0, 0(sp)                /* argc == 1，argv[0] 指向程序路径。 */
    li t1, 1
    bne t0, t1, .Lfail
    ld t0, 8(sp)
    lbu t0, 0(t0)
    li t1, '/'
    bne t0, t1, .Lfail
    addi sp, sp, -64
    open_demo O_CREAT|O_WRONLY|O_TRUNC
    bltz a0, .Lfail
    mv s1, a0
    la a1, message
    li a2, 3
    li a7, __NR_write
    ecall
    li t0, 3
    bne a0, t0, .Lfail
    mv a0, s1
    li a7, __NR_close
    ecall
    bnez a0, .Lfail

    open_demo O_RDONLY
    bltz a0, .Lfail
    mv s2, a0
    expect_byte s2, 'a'
    open_demo O_RDONLY
    bltz a0, .Lfail
    mv s1, a0
    expect_byte s1, 'a'          /* 同一 inode，独立的打开位置。 */
    mv a0, s1
    li a7, __NR_close
    ecall
    bnez a0, .Lfail

    addi a0, sp, 8               /* int pipefd[2] */
    li a1, 0
    li a7, __NR_pipe2
    ecall
    bnez a0, .Lfail
    li a0, SIGCHLD
    li a1, 0
    li a7, __NR_clone
    ecall
    bltz a0, .Lfail
    beqz a0, .Lchild
    mv s0, a0
    lw a0, 12(sp)
    li a7, __NR_close
    ecall
    bnez a0, .Lfail
    mv a0, s0
    mv a1, sp
    li a2, 0
    li a3, 0
    li a7, __NR_wait4
    ecall
    bne a0, s0, .Lfail
    lw t0, 0(sp)
    /* 两种验收盘各有一个已知退出码，保留完整 wait 状态比较。 */
    li t1, (42 << 8)
    beq t0, t1, .Lreport
    li t1, (73 << 8)
    bne t0, t1, .Lfail
.Lreport:
    srli t0, t0, 8
    li t1, 10
    divu t2, t0, t1
    remu t0, t0, t1
    addi t2, t2, '0'
    addi t0, t0, '0'
    la t1, status_digits
    sb t2, 0(t1)
    sb t0, 1(t1)
    li a0, 1
    la a1, status_message
    li a2, STATUS_LEN
    li a7, __NR_write
    ecall
    li t0, STATUS_LEN
    bne a0, t0, .Lfail
    /* 子进程退出时关闭最后一个写端；先取出管道中剩余的数据。 */
    lw a0, 8(sp)
    addi a1, sp, 16
    li a2, 48
    li a7, __NR_read
    ecall
    li t0, PIPE_MESSAGE_LEN
    bne a0, t0, .Lfail
    mv a2, a0
    li a0, 1
    li a7, __NR_write
    ecall
    li t0, PIPE_MESSAGE_LEN
    bne a0, t0, .Lfail
    lw a0, 8(sp)
    li a2, 1
    li a7, __NR_read
    ecall
    bnez a0, .Lfail
    lw a0, 8(sp)
    li a7, __NR_close
    ecall
    bnez a0, .Lfail
    la t0, child_argv
    ld t0, 8(t0)                /* 子进程填写 argv[1] 后，父进程仍应读到 NULL。 */
    bnez t0, .Lfail
    expect_byte s2, 'c'          /* 子进程已通过继承的 fd 读了 b。 */
    mv a0, s2
    li a7, __NR_close
    ecall
    bnez a0, .Lfail
    li a0, 0
    j .Lexit

.Lchild:
    lw a0, 8(sp)
    li a7, __NR_close
    ecall
    bnez a0, .Lfail
    lw a0, 12(sp)
    la a1, pipe_message
    li a2, PIPE_MESSAGE_LEN
    li a7, __NR_write
    ecall
    li t0, PIPE_MESSAGE_LEN
    bne a0, t0, .Lfail
    la a0, child_path
    la a1, child_argv
    la t0, argument
    sd t0, 8(a1)                /* 写共享数据页触发 COW，execve 从私有副本读取参数。 */
    /* 将实际 fd 作为十进制 argv[2] 传给新映像。 */
    la t0, fd_arg_end
    li t1, 10
.Lfd_digit:
    remu t2, s2, t1
    divu s2, s2, t1
    addi t2, t2, '0'
    addi t0, t0, -1
    sb t2, 0(t0)
    bnez s2, .Lfd_digit
    sd t0, 16(a1)
    la a2, child_envp
    li a7, __NR_execve
    ecall
.Lfail:
    li a0, 1
.Lexit:
    li a7, __NR_exit
    ecall

    .section .rodata
demo_path:  .asciz "/tmp/fd-demo"
message:    .ascii "abc"
child_path: .asciz "/disk/bin/child"
argument:   .asciz "42"
environment: .asciz "MINUX=1"

    .data
    .balign 8
child_argv: .dword child_path, 0, 0, 0
child_envp: .dword environment, 0
    .skip 10
fd_arg_end: .byte 0
