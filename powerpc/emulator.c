/*
 * Test some powerpc instructions
 */

#include <libcflat.h>
#include <asm/processor.h>

static int volatile is_invalid;
static int volatile alignment;

static void program_check_handler(struct pt_regs *regs, void *opaque)
{
	int *data = opaque;

	printf("Detected invalid instruction 0x%016lx: %08x\n",
	       regs->nip, *(uint32_t*)regs->nip);

	*data = 1;

	regs->nip += 4;
}

static void alignment_handler(struct pt_regs *regs, void *opaque)
{
	int *data = opaque;

	printf("Detected alignment exception 0x%016lx: %08x\n",
	       regs->nip, *(uint32_t*)regs->nip);

	*data = 1;

	regs->nip += 4;
}

static void test_illegal(void)
{
	report_prefix_push("invalid");

	is_invalid = 0;

	asm volatile (".long 0");

	report("exception", is_invalid);

	report_prefix_pop();
}

static void test_64bit(void)
{
	uint64_t msr;

	report_prefix_push("64bit");

	asm("mfmsr %[msr]": [msr] "=r" (msr));

	report("detected", msr & 0x8000000000000000UL);

	report_prefix_pop();
}

/*
 * lswx: Load String Word Indexed X-form
 *
 *     lswx RT,RA,RB
 *
 * EA = (RA|0) + RB
 * n  = XER
 *
 * Load n bytes from address EA into (n / 4) consecutive registers,
 * throught RT -> RT + (n / 4) - 1.
 * - Data are loaded into 4 low order bytes of registers (Word).
 * - The unfilled bytes are set to 0.
 * - The sequence of registers wraps around to GPR0.
 * - if n == 0, content of RT is undefined
 * - RT <= RA or RB < RT + (n + 4) is invalid or result is undefined
 * - RT == RA == 0 is invalid
 *
 * For lswx in little-endian mode, an alignment interrupt always occurs.
 *
 */

#define SPR_XER	1

static void test_lswx(void)
{
	int i;
	char addr[128];
	uint64_t regs[32];

	report_prefix_push("lswx");

	/* fill memory with sequence */

	for (i = 0; i < 128; i++)
		addr[i] = 1 + i;

	/* check incomplete register filling */

	alignment = 0;
	asm volatile ("mtspr %[XER], %[len];"
		      "li r12,-1;"
		      "mr r11, r12;"
		      "lswx r11, 0, %[addr];"
		      "std r11, 0*8(%[regs]);"
		      "std r12, 1*8(%[regs]);"
		      ::
		      [len] "r" (3),
		      [XER] "i" (SPR_XER),
		      [addr] "r" (addr),
		      [regs] "r" (regs)
		      :
		      /* as 32 is the number of bytes,
		       * we should modify 32/4 = 8 regs, from r1
		       */
		      "xer", "r11", "r12");

#if  __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	report("alignment", alignment);
	return;
#else
	report("partial", regs[0] == 0x01020300 && regs[1] == (uint64_t)-1);
#endif

	/* check an old know bug: the number of bytes is used as
	 * the number of registers, so try 32 bytes.
	 */

	asm volatile ("mtspr %[XER], %[len];"
		      "li r19,-1;"
		      "mr r11, r19; mr r12, r19; mr r13, r19;"
		      "mr r14, r19; mr r15, r19; mr r16, r19;"
		      "mr r17, r19; mr r18, r19;"
		      "lswx r11, 0, %[addr];"
		      "std r11, 0*8(%[regs]);"
		      "std r12, 1*8(%[regs]);"
		      "std r13, 2*8(%[regs]);"
		      "std r14, 3*8(%[regs]);"
		      "std r15, 4*8(%[regs]);"
		      "std r16, 5*8(%[regs]);"
		      "std r17, 6*8(%[regs]);"
		      "std r18, 7*8(%[regs]);"
		      "std r19, 8*8(%[regs]);"
		      ::
		      [len] "r" (32),
		      [XER] "i" (SPR_XER),
		      [addr] "r" (addr),
		      [regs] "r" (regs)
		      :
		      /* as 32 is the number of bytes,
		       * we should modify 32/4 = 8 regs, from r1
		       */
		      "xer", "r11", "r12", "r13", "r14", "r15", "r16", "r17",
		      "r18", "r19");

	report("length", regs[0] == 0x01020304 && regs[1] == 0x05060708 &&
			 regs[2] == 0x090a0b0c && regs[3] == 0x0d0e0f10 &&
			 regs[4] == 0x11121314 && regs[5] == 0x15161718 &&
			 regs[6] == 0x191a1b1c && regs[7] == 0x1d1e1f20 &&
			 regs[8] == (uint64_t)-1);

	/* check wrap around to r0 */

	asm volatile ("mtspr %[XER], %[len];"
		      "li r31,-1;"
		      "mr r0, r31;"
		      "lswx r31, 0, %[addr];"
		      "std r31, 0*8(%[regs]);"
		      "std r0, 1*8(%[regs]);"
		      ::
		      [len] "r" (8),
		      [XER] "i" (SPR_XER),
		      [addr] "r" (addr),
		      [regs] "r" (regs)
		      :
		      /* as 32 is the number of bytes,
		       * we should modify 32/4 = 8 regs, from r1
		       */
		      "xer", "r31", "r0");

	report("wrap around to r0", regs[0] == 0x01020304 &&
			            regs[1] == 0x05060708);

	/* check wrap around to r0 over RB doesn't break RB */

	asm volatile ("mtspr %[XER], %[len];"
		      /* adding r1 in the clobber list doesn't protect it... */
		      "mr r29,r1;"
		      "li r31,-1;"
		      "mr r1,r31;"
		      "mr r0, %[addr];"
		      "lswx r31, 0, r0;"
		      "std r31, 0*8(%[regs]);"
		      "std r0, 1*8(%[regs]);"
		      "std r1, 2*8(%[regs]);"
		      "mr r1,r29;"
		      ::
		      [len] "r" (12),
		      [XER] "i" (SPR_XER),
		      [addr] "r" (addr),
		      [regs] "r" (regs)
		      :
		      /* as 32 is the number of bytes,
		       * we should modify 32/4 = 8 regs, from r1
		       */
		      "xer", "r31", "r0", "r29");

	/* doc says it is invalid, real proc stops when it comes to
	 * overwrite the register.
	 * In all the cases, the register must stay untouched
	 */
	report("Don't overwrite Rb", regs[1] == (uint64_t)addr);

	report_prefix_pop();
}

int main(void)
{
	handle_exception(0x700, program_check_handler, (void *)&is_invalid);
	handle_exception(0x600, alignment_handler, (void *)&alignment);

	report_prefix_push("emulator");

	test_64bit();
	test_illegal();
	test_lswx();

	report_prefix_pop();

	return report_summary();
}
