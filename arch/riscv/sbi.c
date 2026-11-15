#include <asm/sbi.h>

struct sbiret {
	long error;
	long value;
};

static struct sbiret sbi_ecall(unsigned long ext, unsigned long fid,
			       unsigned long arg0, unsigned long arg1)
{
	register unsigned long a0 asm("a0") = arg0;
	register unsigned long a1 asm("a1") = arg1;
	register unsigned long a6 asm("a6") = fid;
	register unsigned long a7 asm("a7") = ext;

	asm volatile("ecall"
		     : "+r"(a0), "+r"(a1)
		     : "r"(a6), "r"(a7)
		     : "memory");

	return (struct sbiret){ .error = a0, .value = a1 };
}

void sbi_console_putchar(int ch)
{
	sbi_ecall(SBI_EXT_0_1_CONSOLE_PUTCHAR, 0, ch, 0);
}

void sbi_shutdown(void)
{
	sbi_ecall(SBI_EXT_SRST, SBI_SRST_RESET,
		  SBI_SRST_TYPE_SHUTDOWN, SBI_SRST_REASON_NONE);

	/* 关机失败时不能返回，原地停住 */
	for (;;)
		asm volatile("wfi");
}
