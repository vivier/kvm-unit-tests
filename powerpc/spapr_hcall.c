/*
 * Test sPAPR hypervisor calls (aka. h-calls)
 *
 * Copyright 2016  Thomas Huth, Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <util.h>
#include <alloc.h>
#include <asm/hcall.h>
#include <asm/processor.h>

#define PAGE_SIZE 4096

#define H_ZERO_PAGE	(1UL << (63-48))
#define H_COPY_PAGE	(1UL << (63-49))

#define mfspr(nr) ({ \
	uint64_t ret; \
	asm volatile("mfspr %0,%1" : "=r"(ret) : "i"(nr)); \
	ret; \
})

#define SPR_SPRG0	0x110
#define SPR_LPCR	0x13E

#define GET_AIL(a)	(((a) >> 23) & 3)

static int volatile is_invalid;
static void program_check_handler(struct pt_regs *regs, void *opaque)
{
        int *data = opaque;
        *data = regs->msr >> 16;

        regs->nip += 4;
}

/**
 * Test the H_SET_SPRG0 h-call by setting some values and checking whether
 * the SPRG0 register contains the correct values afterwards
 */
static void test_h_set_sprg0(int argc, char **argv)
{
	uint64_t sprg0, sprg0_orig;
	int rc;

	if (argc > 1)
		report_abort("Unsupported argument: '%s'", argv[1]);

	sprg0_orig = mfspr(SPR_SPRG0);

	rc = hcall(H_SET_SPRG0, 0xcafebabedeadbeefULL);
	sprg0 = mfspr(SPR_SPRG0);
	report("sprg0 = 0xcafebabedeadbeef",
		rc == H_SUCCESS && sprg0 == 0xcafebabedeadbeefULL);

	rc = hcall(H_SET_SPRG0, 0xaaaaaaaa55555555ULL);
	sprg0 = mfspr(SPR_SPRG0);
	report("sprg0 = 0xaaaaaaaa55555555",
		rc == H_SUCCESS && sprg0 == 0xaaaaaaaa55555555ULL);

	rc = hcall(H_SET_SPRG0, sprg0_orig);
	sprg0 = mfspr(SPR_SPRG0);
	report("sprg0 = 0x%llx",
		rc == H_SUCCESS && sprg0 == sprg0_orig, sprg0_orig);
}

/**
 * Test the H_PAGE_INIT h-call by using it to clear and to copy a page, and
 * by checking for the correct values in the destination page afterwards
 */
static void test_h_page_init(int argc, char **argv)
{
	u8 *dst, *src;
	int rc;

	if (argc > 1)
		report_abort("Unsupported argument: '%s'", argv[1]);

	dst = memalign(PAGE_SIZE, PAGE_SIZE);
	src = memalign(PAGE_SIZE, PAGE_SIZE);
	if (!dst || !src)
		report_abort("Failed to alloc memory");

	memset(dst, 0xaa, PAGE_SIZE);
	rc = hcall(H_PAGE_INIT, H_ZERO_PAGE, dst, src);
	report("h_zero_page", rc == H_SUCCESS && *(uint64_t*)dst == 0);

	*(uint64_t*)src = 0xbeefc0dedeadcafeULL;
	rc = hcall(H_PAGE_INIT, H_COPY_PAGE, dst, src);
	report("h_copy_page",
		rc == H_SUCCESS && *(uint64_t*)dst == 0xbeefc0dedeadcafeULL);

	*(uint64_t*)src = 0x9abcdef012345678ULL;
	rc = hcall(H_PAGE_INIT, H_COPY_PAGE|H_ZERO_PAGE, dst, src);
	report("h_copy_page+h_zero_page",
		rc == H_SUCCESS &&  *(uint64_t*)dst == 0x9abcdef012345678ULL);

	rc = hcall(H_PAGE_INIT, H_ZERO_PAGE, dst + 0x123, src);
	report("h_zero_page unaligned dst", rc == H_PARAMETER);

	rc = hcall(H_PAGE_INIT, H_COPY_PAGE, dst, src + 0x123);
	report("h_copy_page unaligned src", rc == H_PARAMETER);
}

static int h_random(uint64_t *val)
{
	register uint64_t r3 asm("r3") = H_RANDOM;
	register uint64_t r4 asm("r4");

	asm volatile (" sc 1 "	: "+r"(r3), "=r"(r4) :
				: "r0", "r5", "r6", "r7", "r8", "r9", "r10",
				  "r11", "r12", "xer", "ctr", "cc");
	*val = r4;

	return r3;
}

/**
 * Test H_RANDOM by calling it a couple of times to check whether all bit
 * positions really toggle (there should be no "stuck" bits in the output)
 */
static void test_h_random(int argc, char **argv)
{
	uint64_t rval, val0, val1;
	int rc, i;

	if (argc > 1)
		report_abort("Unsupported argument: '%s'", argv[1]);

	/* H_RANDOM is optional - so check for sane return values first */
	rc = h_random(&rval);
	report_xfail("h-call available", rc == H_FUNCTION, rc == H_SUCCESS);
	if (rc != H_SUCCESS)
		return;

	val0 = 0ULL;
	val1 = ~0ULL;

	i = 100;
	do {
		rc = h_random(&rval);
		if (rc != H_SUCCESS)
			break;
		val0 |= rval;
		val1 &= rval;
	} while (i-- > 0 && (val0 != ~0ULL || val1 != 0ULL));

	report("no stuck bits", rc == H_SUCCESS && val0 == ~0ULL && val1 == 0);
}

/*
 * Test H_SET_MODE h_call()
 *
 * Note: H_SET_MODE(H_SET_MODE_RESOURCE_LE) is already
 *       called in cpu_init() to set endianness of the interrupt
 *       handler.
 */

static void test_h_set_mode(int argc, char **argv)
{
	int rc;
	uint64_t old_AIL, lpcr;

	if (argc > 1)
		report_abort("Unsupported argument: '%s'", argv[1]);

	/* Address Translation Mode on Interrupt
	 * (set Alternate Interrupt Location in LPCR)
	 * Note: only supported on POWER ISA level 2.07 and beyond
	 */

	report_prefix_push("AIL");

	lpcr = mfspr(SPR_LPCR);
	old_AIL = GET_AIL(lpcr);
	/* check reserved value fails */

	rc = hcall(H_SET_MODE, AIL_RESERVED,
		   H_SET_MODE_RESOURCE_ADDR_TRANS_MODE, 0, 0);
	report_xfail("Reserved", rc == H_P2, rc == H_UNSUPPORTED_FLAG);
	if (rc == H_P2)
		goto out;

	/* check the normal mode */

	rc = hcall(H_SET_MODE, AIL_NONE,
		   H_SET_MODE_RESOURCE_ADDR_TRANS_MODE, 0, 0);
	report("Set Mode 0", rc == H_SUCCESS);

	/* check value1 cannot be set */

	rc = hcall(H_SET_MODE, AIL_NONE,
		   H_SET_MODE_RESOURCE_ADDR_TRANS_MODE, 1, 0);
	report("value1", rc == H_P3);

	/* check value2 cannot be set */

	rc = hcall(H_SET_MODE, AIL_NONE,
		   H_SET_MODE_RESOURCE_ADDR_TRANS_MODE, 0, 1);
	report("value2", rc == H_P4);

	/* check LPCR */

	lpcr = mfspr(SPR_LPCR);
	report("Check LPCR Mode 0", GET_AIL(lpcr) == AIL_NONE);

	/* Address 0x000180000 */

	rc = hcall(H_SET_MODE, AIL_0001_8000,
		   H_SET_MODE_RESOURCE_ADDR_TRANS_MODE, 0, 0);
	report("Set Mode 2", rc == H_SUCCESS);

	/* check LPCR */

	lpcr = mfspr(SPR_LPCR);
	report("check LPCR Mode 2", GET_AIL(lpcr) == AIL_0001_8000);

	/* Address C000_0000_0000_4000 */

	rc = hcall(H_SET_MODE, AIL_C000_0000_0000_4000,
		   H_SET_MODE_RESOURCE_ADDR_TRANS_MODE, 0, 0);
	report("Set Mode 3", rc == H_SUCCESS);

	/* check LPCR */

	lpcr = mfspr(SPR_LPCR);
	report("Check LPCR Mode 3", GET_AIL(lpcr) == AIL_C000_0000_0000_4000);

	/* Restore AIL value */

	rc = hcall(H_SET_MODE, old_AIL,
		   H_SET_MODE_RESOURCE_ADDR_TRANS_MODE, 0, 0);
	report("Restore original AIL", rc == H_SUCCESS);

	/* this should not crash */

	handle_exception(0x700, program_check_handler, (void *)&is_invalid);
	is_invalid = 0;

	asm volatile(".long 0"); /* invalid instruction */

	report("Check exception", is_invalid == 8);

out:
	report_prefix_pop();
}

struct {
	const char *name;
	void (*func)(int argc, char **argv);
} hctests[] = {
	{ "h_set_sprg0", test_h_set_sprg0 },
	{ "h_page_init", test_h_page_init },
	{ "h_random", test_h_random },
	{ "h_set_mode", test_h_set_mode },
	{ NULL, NULL }
};

int main(int argc, char **argv)
{
	int all = 0;
	int i;

	report_prefix_push("hypercall");

	if (!argc || (argc == 1 && !strcmp(argv[0], "all")))
		all = 1;

	for (i = 0; hctests[i].name != NULL; i++) {
		report_prefix_push(hctests[i].name);
		if (all || strcmp(argv[0], hctests[i].name) == 0) {
			hctests[i].func(argc, argv);
		}
		report_prefix_pop();
	}

	return report_summary();
}
