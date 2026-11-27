#include <asm/unistd.h>
#include <minux/errno.h>
#include <minux/fcntl.h>
#include <minux/fdtable.h>
#include <minux/mman.h>
#include <minux/signal.h>
#include <minux/syscall.h>
#include "user_wait_probe.h"

    .macro probe_action action
    li      a0, \action
    li      a7, __NR_test_wait
    ecall
    .endm

    .macro wait4 pid, status, opts=0
    mv      a0, \pid
    mv      a1, \status
    li      a2, \opts
    li      a3, 0
    li      a7, __NR_wait4
    ecall
    .endm

    .section .rodata
    .align 2
    .globl wait_probe_start, wait_probe_end
    .globl wait_probe_parent_first, wait_probe_child_first
    .globl wait_probe_parent_fault, wait_probe_parent_orphans
    .globl wait_probe_fork, wait_probe_fork_oom, wait_probe_exec_errors
    .globl wait_probe_exec
    .globl wait_probe_child, wait_probe_fault
wait_probe_start:
wait_probe_parent_first:
    addi    sp, sp, -16
    li      t0, -1
    sd      t0, 0(sp)
    probe_action WAIT_PROBE_SPAWN
    blez    a0, .Lfail
    mv      s0, a0
    li      s1, -1

    wait4   s1, sp, 2           /* 尚未实现的选项。 */
    li      t0, -EINVAL
    bne     a0, t0, .Lfail
    addi    t1, s0, 1
    wait4   t1, sp              /* 有活子进程，但是 PID 不匹配。 */
    li      t0, -ECHILD
    bne     a0, t0, .Lfail
    wait4   s1, sp, WNOHANG
    bnez    a0, .Lfail
    ld      t0, 0(sp)
    bne     t0, s1, .Lfail      /* 无退出状态时不写用户缓冲区。 */

    wait4   s0, sp
    bne     a0, s0, .Lfail
    lw      t0, 0(sp)
    li      t1, WAIT_PROBE_EXIT_STATUS
    bne     t0, t1, .Lfail
    lw      t0, 4(sp)
    bne     t0, s1, .Lfail      /* wait4 只写 4 字节状态。 */
    wait4   s1, zero
    li      t0, -ECHILD
    bne     a0, t0, .Lfail
    j       .Ldone

wait_probe_child_first:
    addi    sp, sp, -16
    li      s1, -1
    probe_action WAIT_PROBE_SPAWN_EXITED
    blez    a0, .Lfail
    mv      s0, a0
    wait4   s1, sp, WNOHANG
    bne     a0, s0, .Lfail
    lw      t0, 0(sp)
    li      t1, WAIT_PROBE_EXIT_STATUS
    bne     t0, t1, .Lfail

    probe_action WAIT_PROBE_SPAWN_EXITED
    blez    a0, .Lfail
    mv      s0, a0
    li      t1, 1               /* 空页保留，状态地址无效。 */
    wait4   s0, t1
    li      t0, -EFAULT
    bne     a0, t0, .Lfail
    wait4   s0, zero            /* 写回失败时子进程也已回收。 */
    li      t0, -ECHILD
    bne     a0, t0, .Lfail

    probe_action WAIT_PROBE_SPAWN_EXITED
    blez    a0, .Lfail
    mv      s0, a0
    wait4   s0, zero
    bne     a0, s0, .Lfail
    j       .Ldone

wait_probe_parent_fault:
    addi    sp, sp, -16
    probe_action WAIT_PROBE_SPAWN_FAULT
    blez    a0, .Lfail
    mv      s0, a0
    wait4   s0, sp
    bne     a0, s0, .Lfail
    lw      t0, 0(sp)
    li      t1, SIGTRAP
    bne     t0, t1, .Lfail
    j       .Ldone

wait_probe_exec:
    addi    sp, sp, -32
    li      a0, SIGCHLD
    li      a1, 0
    li      a7, __NR_clone
    ecall
    bltz    a0, .Lfail
    beqz    a0, .Lexec_init
    mv      s0, a0
    wait4   s0, sp
    bne     a0, s0, .Lfail
    lw      t0, 0(sp)
    bnez    t0, .Lfail
    j       .Ldone
.Lexec_init:
    /* 自测在 UART 启用前运行，以空文件作为 stdin 使 shell 读到 EOF。 */
    li      a0, AT_FDCWD
    la      a1, .Lempty_stdin
    li      a2, O_RDONLY|O_CREAT
    li      a3, 0600
    li      a7, __NR_openat
    ecall
    bnez    a0, .Lfail
    li      a0, AT_FDCWD
    la      a1, .Lconsole
    li      a2, O_WRONLY
    li      a7, __NR_openat
    ecall
    li      t0, 1
    bne     a0, t0, .Lfail
    li      a0, AT_FDCWD
    la      a1, .Lconsole
    li      a2, O_WRONLY
    li      a7, __NR_openat
    ecall
    li      t0, 2
    bne     a0, t0, .Lfail
    la      a0, .Linit_path
    sd      a0, 0(sp)
    sd      zero, 8(sp)
    mv      a1, sp
    li      a2, 0
    li      a7, __NR_execve
    ecall
    j       .Lfail

    .globl wait_probe_pipes
wait_probe_pipes:
    /* 第二个 int 落在未映射页，失败时第一个 int 和 fd 表都应保持原样。 */
    addi    sp, sp, -16
    li      t0, 123
    sw      t0, 12(sp)
    addi    a0, sp, 12
    li      a1, 0
    li      a7, __NR_pipe2
    ecall
    li      t0, -EFAULT
    bne     a0, t0, .Lfail
    lw      t0, 12(sp)
    li      t1, 123
    bne     t0, t1, .Lfail
    /* 用户缓冲区的最后四字节可访问。短写和短读都只提交已复制的数据。 */
    mv      a0, sp
    li      a1, 0
    li      a7, __NR_pipe2
    ecall
    bnez    a0, .Lfail
    lw      a0, 4(sp)
    addi    a1, sp, 12
    li      a2, 8
    li      a7, __NR_write
    ecall
    li      t0, 4
    bne     a0, t0, .Lfail
    lw      a0, 4(sp)
    li      a2, 4
    ecall
    bne     a0, t0, .Lfail
    sw      zero, 12(sp)
    lw      a0, 0(sp)
    li      a2, 8
    li      a7, __NR_read
    ecall
    bne     a0, t0, .Lfail
    lw      a0, 0(sp)
    li      a2, 4
    ecall
    bne     a0, t0, .Lfail
    lw      t1, 12(sp)
    li      t0, 123
    bne     t1, t0, .Lfail
    lw      a0, 0(sp)
    li      a7, __NR_close
    ecall
    bnez    a0, .Lfail
    lw      a0, 4(sp)
    ecall
    bnez    a0, .Lfail
    la      s0, .Lpipes
    j       .Lrun_test

    .globl wait_probe_commands
wait_probe_commands:
    addi    sp, sp, -16
    la      s0, .Lcommands
.Lrun_test:
    li      a0, SIGCHLD
    li      a1, 0
    li      a7, __NR_clone
    ecall
    bltz    a0, .Lfail
    beqz    a0, .Lexec_test
    mv      s0, a0
    wait4   s0, sp
    bne     a0, s0, .Lfail
    lw      t0, 0(sp)
    bnez    t0, .Lfail
    j       .Ldone
.Lexec_test:
    mv      a0, s0
    li      a1, 0
    li      a2, 0
    li      a7, __NR_execve
    ecall
    j       .Lfail

wait_probe_exec_errors:
    addi    sp, sp, -288
    li      s1, 0x1234
    sd      s1, 0(sp)
    li      a7, __NR_execve
    la      a0, .Ltruncated
    li      a1, 0
    li      a2, 0
    ecall
    li      t0, -ENOEXEC
    bne     a0, t0, .Lfail
    la      a0, .Lmissing
    ecall
    li      t0, -ENOENT
    bne     a0, t0, .Lfail
    li      a0, 1
    ecall
    li      t0, -EFAULT
    bne     a0, t0, .Lfail
    la      s0, .Lchild_path
    mv      a0, s0
    li      a2, 1
    ecall
    li      t0, -EFAULT
    bne     a0, t0, .Lfail
    mv      a0, s0
    li      a2, 0
    addi    a1, sp, 8
    li      t0, 1
    sd      t0, 0(a1)          /* 无效 argv[0]。 */
    ecall
    li      t0, -EFAULT
    bne     a0, t0, .Lfail
    mv      t0, a1
    li      t1, 33
.Lfill_argv:
    sd      s0, 0(t0)
    addi    t0, t0, 8
    addi    t1, t1, -1
    bnez    t1, .Lfill_argv
    sd      zero, 0(t0)
    mv      a0, s0
    ecall
    li      t0, -E2BIG
    bne     a0, t0, .Lfail
    ld      t0, 0(sp)
    bne     t0, s1, .Lfail
    j       .Ldone

    .macro fd_open path, flags, dirfd=AT_FDCWD
    li      a0, \dirfd
    la      a1, \path
    li      a2, (\flags)
    li      a3, 0600
    li      a7, __NR_openat
    ecall
    .endm

    .macro expect_result result
    li      t0, \result
    bne     a0, t0, .Lfail
    .endm

    .macro tty_read dst, count, result
    li      a0, 0
    mv      a1, \dst
    li      a2, \count
    li      a7, __NR_read
    ecall
    expect_result \result
    .endm

    .globl wait_probe_tty
wait_probe_tty:
    addi    sp, sp, -16
    fd_open .Lconsole, O_RDONLY
    expect_result 0
    /* 输入队列已有 "abc^D"、空行、空 EOF 和 "Z\n"。 */
    tty_read zero, 1, -EFAULT
    tty_read sp, 2, 2
    lhu     t1, 0(sp)
    li      t0, 0x6261
    bne     t0, t1, .Lfail
    tty_read sp, 1, 1           /* 返回 c 的同一次 read 消耗行尾 ^D。 */
    lbu     t1, 0(sp)
    li      t0, 'c'
    bne     t0, t1, .Lfail
    tty_read sp, 8, 1
    lbu     t1, 0(sp)
    li      t0, 10
    bne     t0, t1, .Lfail
    tty_read sp, 0, 0
    tty_read sp, 8, 0
    tty_read sp, 8, 2
    lhu     t1, 0(sp)
    li      t0, 0x0a5a
    bne     t0, t1, .Lfail
    j       .Ldone

    .globl wait_probe_files
wait_probe_files:
    /* 栈顶之后无映射。跨页复制只提交前两个字节。 */
    addi    s1, sp, -2
    addi    sp, sp, -1024
    li      t0, 0x4241
    sh      t0, 0(s1)
    fd_open .Lrelative_file, O_CREAT|O_EXCL|O_WRONLY
    expect_result 0
    fd_open .Lrelative_file, O_RDONLY, 0
    expect_result -ENOTDIR
    fd_open .Lrelative_file, O_RDONLY, 17
    expect_result -EBADF
    li      a0, 0
    mv      a1, s1
    li      a2, 4
    li      a7, __NR_read
    ecall
    expect_result -EBADF
    li      a0, 0
    li      a7, __NR_write
    ecall
    expect_result 2
    li      a0, 0
    li      a1, 0
    ecall
    expect_result -EFAULT
    li      a0, 0
    mv      a1, sp
    li      a2, 700
    ecall                       /* 一次写入跨多个内核中转缓冲。 */
    expect_result 700
    li      a0, 0
    li      a7, __NR_close
    ecall
    expect_result 0
    li      a0, 0
    ecall
    expect_result -EBADF

    li      a0, AT_FDCWD
    li      a1, 1
    li      a2, O_RDONLY
    li      a7, __NR_openat
    ecall
    expect_result -EFAULT
    fd_open .Labsolute_file, O_RDONLY, 99
    expect_result 0
    li      a0, 0
    li      a1, 0
    li      a2, 0
    li      a7, __NR_read
    ecall
    expect_result 0
    li      a0, 0
    li      a2, 4
    ecall
    expect_result -EFAULT
    li      a0, 0
    mv      a1, s1
    sh      zero, 0(s1)
    ecall
    expect_result 2
    lhu     t0, 0(s1)
    li      t1, 0x4241
    bne     t0, t1, .Lfail
    li      a0, 0
    mv      a1, sp
    li      a2, 710
    ecall
    expect_result 700
    li      a0, 0
    ecall
    expect_result 0
    li      a0, 0
    li      a7, __NR_write
    ecall
    expect_result -EBADF

    /* 表满的 openat 不能创建新文件，也不能截断旧文件。 */
    li      s2, 1
.Lfill_fds:
    fd_open .Labsolute_file, O_RDONLY
    bne     a0, s2, .Lfail
    addi    s2, s2, 1
    li      t0, FD_MAX
    blt     s2, t0, .Lfill_fds
    fd_open .Labsolute_file, O_WRONLY|O_TRUNC
    expect_result -EMFILE
    fd_open .Lmissing, O_WRONLY|O_CREAT
    expect_result -EMFILE
    li      a0, 1
    li      a7, __NR_close
    ecall
    expect_result 0
    fd_open .Lmissing, O_RDONLY
    expect_result -ENOENT
    fd_open .Labsolute_file, O_RDONLY
    expect_result 1
    mv      a1, sp
    li      a2, 2
    li      a7, __NR_read
    ecall
    expect_result 2
    lhu     t0, 0(sp)
    li      t1, 0x4241
    bne     t0, t1, .Lfail
    j       .Ldone              /* 让 exit 释放所有打开文件。 */

wait_probe_fork_oom:
    li      a7, __NR_clone
    li      a0, SIGCHLD
    li      a1, 0
    ecall
    li      t0, -ENOMEM
    bne     a0, t0, .Lfail
    addi    sp, sp, -16
    sd      t0, 0(sp)
    li      s0, -1
    wait4   s0, zero, WNOHANG
    li      t0, -ECHILD
    bne     a0, t0, .Lfail
    la      a0, .Lchild_path
    li      a1, 0
    li      a2, 0
    li      a7, __NR_execve
    ecall
    ld      t0, 0(sp)          /* 内存不足返回原入口之后，旧栈仍可读写。 */
    bne     a0, t0, .Lfail
    sd      zero, 0(sp)
    j       .Ldone

wait_probe_fork:
    addi    sp, sp, -32
    li      a7, __NR_getpid
    ecall
    mv      s1, a0
    li      s2, FORK_PROBE_ROUNDS

    li      a7, __NR_clone
    li      a0, SIGCHLD
    mv      a1, sp              /* 自定义子栈超出当前 fork 子集。 */
    ecall
    li      t0, -EINVAL
    bne     a0, t0, .Lfail
    li      a0, 0x111           /* CLONE_VM | SIGCHLD */
    li      a1, 0
    ecall
    li      t0, -EINVAL
    bne     a0, t0, .Lfail
.Lfork_round:
    sd      s2, 0(sp)
    li      s3, 0x1234
    sd      s3, 8(sp)
    li      a0, SIGCHLD
    li      a1, 0
    li      a7, __NR_clone
    ecall
    beqz    a0, .Lfork_child
    bltz    a0, .Lfail
    mv      s0, a0
    beq     s0, s1, .Lfail
    andi    t0, s2, 1
    beqz    t0, .Luser_store
    mv      a0, sp              /* copy_to_user 是 fork 后对共享栈的第一次写。 */
    addi    a1, sp, 8
    li      a2, 8
    li      a7, __NR_test_copy
    ecall
    bnez    a0, .Lfail
    j       .Lfork_wait
.Luser_store:
    sd      s3, 0(sp)           /* 父进程直接写，期间没有切换页表。 */
.Lfork_wait:
    addi    s4, sp, 16
    wait4   s0, s4
    bne     a0, s0, .Lfail
    andi    t0, s0, 63
    ori     t0, t0, 64
    slli    t0, t0, 8
    lw      t1, 0(s4)
    bne     t0, t1, .Lfail
    ld      t0, 0(sp)
    bne     t0, s3, .Lfail
    addi    s2, s2, -1
    bnez    s2, .Lfork_round
    j       .Ldone
.Lfork_child:
    ld      t0, 0(sp)
    bne     t0, s2, .Lfail      /* 仍读到 fork 时的栈内容。 */
    sd      zero, 0(sp)
    li      a7, __NR_getpid
    ecall
    beq     a0, s1, .Lfail
    andi    a0, a0, 63
    ori     a0, a0, 64
    j       .Lexit

    .macro do_mmap addr, len, prot, flags, fd=-1, off=0
    mv      a0, \addr
    li      a1, \len
    li      a2, (\prot)
    li      a3, (\flags)
    li      a4, \fd
    li      a5, \off
    li      a7, __NR_mmap
    ecall
    .endm

    .globl wait_probe_mmap
wait_probe_mmap:
    addi    sp, sp, -16
    do_mmap zero, 0, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS
    expect_result -EINVAL
    do_mmap zero, WAIT_PROBE_PAGE_BYTES, PROT_READ, MAP_PRIVATE
    expect_result -EINVAL
    do_mmap zero, WAIT_PROBE_PAGE_BYTES, PROT_READ, MAP_SHARED|MAP_ANONYMOUS
    expect_result -EINVAL
    do_mmap zero, WAIT_PROBE_PAGE_BYTES, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED
    expect_result -EINVAL
    do_mmap zero, WAIT_PROBE_PAGE_BYTES, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|0x100
    expect_result -EINVAL
    do_mmap zero, WAIT_PROBE_PAGE_BYTES, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0
    expect_result -EINVAL
    do_mmap zero, WAIT_PROBE_PAGE_BYTES, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, WAIT_PROBE_PAGE_BYTES
    expect_result -EINVAL
    do_mmap zero, WAIT_PROBE_PAGE_BYTES, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS
    expect_result -EINVAL
    do_mmap zero, WAIT_PROBE_PAGE_BYTES, PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS
    expect_result -EINVAL
    do_mmap zero, WAIT_PROBE_PAGE_BYTES, PROT_READ|0x8, MAP_PRIVATE|MAP_ANONYMOUS
    expect_result -EINVAL
    do_mmap zero, -1, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS
    expect_result -ENOMEM

    li      a0, WAIT_PROBE_PAGE_BYTES
    li      a1, 0
    li      a7, __NR_munmap
    ecall
    expect_result -EINVAL
    li      a0, WAIT_PROBE_PAGE_BYTES + 1
    li      a1, WAIT_PROBE_PAGE_BYTES
    li      a7, __NR_munmap
    ecall
    expect_result -EINVAL
    li      a0, WAIT_PROBE_PAGE_BYTES
    li      a1, -1
    li      a7, __NR_munmap
    ecall
    expect_result -EINVAL

    do_mmap zero, 2 * WAIT_PROBE_PAGE_BYTES + 1, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS
    blez    a0, .Lfail
    li      t0, WAIT_PROBE_PAGE_BYTES - 1
    and     t0, a0, t0
    bnez    t0, .Lfail
    mv      s0, a0
    li      t0, WAIT_PROBE_PAGE_BYTES
    add     s1, s0, t0
    add     s2, s1, t0
    li      t0, 0x11
    sd      t0, 0(s0)
    sd      t0, -8(s1)
    li      t0, 0x22
    sd      t0, 0(s1)
    li      t0, 0x33
    sd      t0, 0(s2)

    li      a0, SIGCHLD
    li      a1, 0
    li      a7, __NR_clone
    ecall
    bltz    a0, .Lfail
    beqz    a0, .Lmmap_cow_child
    mv      s3, a0
    wait4   s3, sp
    bne     a0, s3, .Lfail
    lw      t0, 0(sp)
    bnez    t0, .Lfail
    ld      t0, 0(s0)
    li      t1, 0x11
    bne     t0, t1, .Lfail
    ld      t0, 0(s2)
    li      t1, 0x33
    bne     t0, t1, .Lfail
    li      t0, 0x55
    sd      t0, 0(s0)

    mv      a0, s1
    li      a1, WAIT_PROBE_PAGE_BYTES
    li      a7, __NR_munmap
    ecall
    bnez    a0, .Lfail
    ld      t0, 0(s0)
    li      t1, 0x55
    bne     t0, t1, .Lfail
    li      t0, 0x66
    sd      t0, 0(s2)
    ld      t1, 0(s2)
    bne     t0, t1, .Lfail

    li      a0, SIGCHLD
    li      a1, 0
    li      a7, __NR_clone
    ecall
    bltz    a0, .Lfail
    beqz    a0, .Lmmap_fault_child
    mv      s3, a0
    wait4   s3, sp
    bne     a0, s3, .Lfail
    lw      t0, 0(sp)
    li      t1, SIGSEGV
    bne     t0, t1, .Lfail

    mv      a0, s0
    li      a1, 3 * WAIT_PROBE_PAGE_BYTES
    li      a7, __NR_munmap
    ecall
    bnez    a0, .Lfail
    j       .Ldone

.Lmmap_cow_child:
    ld      t0, 0(s0)
    li      t1, 0x11
    bne     t0, t1, .Lfail
    li      t0, 0xaa
    sd      t0, 0(s0)
    li      t0, 0xcc
    sd      t0, 0(s2)
    ld      t1, 0(s0)
    li      t0, 0xaa
    bne     t0, t1, .Lfail
    li      a0, 0
    j       .Lexit

.Lmmap_fault_child:
    ld      t0, 0(s1)
    j       .Lfail

wait_probe_parent_orphans:
    probe_action WAIT_PROBE_SPAWN_EXITED
    blez    a0, .Lfail
    probe_action WAIT_PROBE_SPAWN
    blez    a0, .Lfail
    /* 父进程退出时同时有僵尸子进程和仍可运行的子进程。 */
.Ldone:
    probe_action WAIT_PROBE_PARENT_DONE
    li      a0, 0
    j       .Lexit
.Lfail:
    li      a0, 1
    j       .Lexit

wait_probe_child:
    probe_action WAIT_PROBE_CHILD_STARTED
    li      a0, WAIT_PROBE_EXIT_CODE
.Lexit:
    li      a7, __NR_exit
    ecall

wait_probe_fault:
    ebreak
    j       wait_probe_fault
.Lrelative_file: .asciz "tmp/fd-probe"
.Labsolute_file: .asciz "/tmp/fd-probe"
.Ltruncated: .asciz "/test/truncated"
.Lmissing: .asciz "/no-such-file"
.Lpipes: .asciz "/test/pipes"
.Lcommands: .asciz "/test/commands"
.Lconsole: .asciz "/dev/console"
.Lempty_stdin: .asciz "/tmp/empty-stdin"
.Linit_path: .asciz "/init"
.Lchild_path: .asciz "/disk/bin/child"
wait_probe_end:
