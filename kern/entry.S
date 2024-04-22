#include <asm/asm.h>
#include <stackframe.h>

.section .text.tlb_miss_entry
tlb_miss_entry:
	j       exc_gen_entry

.section .text.exc_gen_entry
exc_gen_entry:
	SAVE_ALL
	/*
	* Note: When EXL=1 || UM=0, the processor is in kernel mode.
	* When EXL is set, the value of EPC is not updated when a new exception occurs.
	* To keep the processor in kernel mode and enable exception reentrancy,
	* we unset UM and EXL, so reentrancy AND kernel mode, and unset IE to globally
	* disable interrupts.
	*/
	mfc0    t0, CP0_STATUS
	and     t0, t0, ~(STATUS_UM | STATUS_EXL | STATUS_IE)
	mtc0    t0, CP0_STATUS

	/* $t0 = ExcCode << 2 */
	mfc0 t0,CP0_CAUSE
	andi t0,0x7c

	lw t0,exception_handlers(t0)
	jr t0
