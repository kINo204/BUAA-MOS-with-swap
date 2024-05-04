#include <asm/asm.h>

// Defined as LEAF, but params passed in to shape stack and $ax.
LEAF(msyscall)
	// Just use 'syscall' instruction and return.
	syscall
	jr ra

END(msyscall)
