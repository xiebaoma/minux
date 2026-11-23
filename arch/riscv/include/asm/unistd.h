#ifndef _ASM_RISCV_UNISTD_H
#define _ASM_RISCV_UNISTD_H

/* Linux RV64 系统调用编号。 */
#define __NR_exit   93
#define __NR_getpid 172
#define __NR_clone  220
#define __NR_wait4  260

#ifdef CONFIG_SELFTEST
#define __NR_test_copy (-2)
#define __NR_test_wait (-3)
#endif

#endif /* _ASM_RISCV_UNISTD_H */
