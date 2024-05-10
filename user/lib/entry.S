#include <asm/asm.h>

.text
EXPORT(_start)
	/* Params argc and argv preallocated, see kern/env.c: 289. */
	lw      a0, 0(sp)
	lw      a1, 4(sp)
	jal     libmain
