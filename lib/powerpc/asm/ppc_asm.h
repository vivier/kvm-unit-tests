#ifndef _ASMPOWERPC_PPC_ASM_H
#define _ASMPOWERPC_PPC_ASM_H

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

#define B_BE(addr)				\
	mtctr	addr;				\
	nop;					\
	bctr

#define RETURN_FROM_BE

#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

#define B_BE(addr)				\
	mfmsr	r11;				\
	xori	r11,r11,1;			\
	mtsrr0	addr;				\
	mtsrr1	r11;				\
	rfid;					\
	b       .

#define RETURN_FROM_BE				\
	.long 0x05000048; /* bl . + 4        */ \
	.long 0xa602487d; /* mflr r10        */	\
	.long 0x20004a39; /* addi r10,r10,32 */	\
	.long 0xa600607d; /* mfmsr r11       */	\
	.long 0x01006b69; /* xori r11,r11,1  */	\
	.long 0xa6035a7d; /* mtsrr0 r10	     */	\
	.long 0xa6037b7d; /* mtsrr1 r11      */	\
	.long 0x2400004c; /* rfid            */ \
	.long 0x00000048; /* b .             */ \

#endif /* __BYTE_ORDER__ */

#endif /* _ASMPOWERPC_PPC_ASM_H */
