#ifndef _ASMPOWERPC_PPC_ASM_H
#define _ASMPOWERPC_PPC_ASM_H

#ifndef __ASSEMBLY__
#error __FILE__ should only be used in assembler files
#else

/* copied from linux arch/powerpc/include/asm/ppc_asm.h */

#define FIXUP_ENDIAN						   \
	tdi   0,0,0x48;	  /* Reverse endian of b . + 8		*/ \
	b     $+36;	  /* Skip trampoline if endian is good	*/ \
	.long 0x05009f42; /* bcl 20,31,$+4			*/ \
	.long 0xa602487d; /* mflr r10				*/ \
	.long 0x1c004a39; /* addi r10,r10,28			*/ \
	.long 0xa600607d; /* mfmsr r11				*/ \
	.long 0x01006b69; /* xori r11,r11,1			*/ \
	.long 0xa6035a7d; /* mtsrr0 r10				*/ \
	.long 0xa6037b7d; /* mtsrr1 r11				*/ \
	.long 0x2400004c  /* rfid				*/
#endif /*  __ASSEMBLY__ */
#endif /* _ASMPOWERPC_PPC_ASM_H */
