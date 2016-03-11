/*
 * Test some powerpc instructions
 */

#include <libcflat.h>
#include <asm/processor.h>

static int volatile is_invalid;

static void program_check_handler(struct pt_regs *regs, void *opaque)
{
	int *data = opaque;

	printf("Detected invalid instruction 0x%016lx: %08x\n",
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

int main(void)
{
	handle_exception(0x700, program_check_handler, (void *)&is_invalid);

	report_prefix_push("emulator");

	test_illegal();

	report_prefix_pop();

	return report_summary();
}
