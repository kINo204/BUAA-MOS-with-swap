#include <bitops.h>
#include <env.h>
#include <pmap.h>
#include <swap.h>
#include <printk.h>

/* Lab 2 Key Code "tlb_invalidate" */
/* Overview:
 *   Invalidate the TLB entry with specified 'asid' and VPN of virtual address 'va'.
 *
 * Hint:
 *   Construct a new Entry HI and call 'tlb_out' to flush TLB.
 *   'tlb_out' is defined in mm/tlb_asm.S
 */
void tlb_invalidate(u_int asid, u_long va) {
	tlb_out((va & ~GENMASK(PGSHIFT, 0)) | (asid & (NASID - 1)));
}
/* End of Key Code "tlb_invalidate" */

static void passive_alloc(u_int va, Pde *pgdir, u_int asid) {
	struct Page *p = NULL;

	if (va < UTEMP) {
		panic("address too low"); }
	if (va >= USTACKTOP && va < USTACKTOP + PAGE_SIZE) {
		panic("invalid memory"); }
	if (va >= UENVS && va < UPAGES) {
		panic("envs zone"); }
	if (va >= UPAGES && va < UVPT) {
		panic("pages zone"); }
	if (va >= ULIM) {
		panic("kernel address"); }

	panic_on(page_alloc(&p));

	u_int perm = (va >= UVPT && va < ULIM) ? 0 : PTE_D;
	panic_on(page_insert(pgdir, asid, p, PTE_ADDR(va), perm));

	if (va < USTACKTOP - PAGE_SIZE) {
		//if (va == 0x443ffffc) { printk("register!\n"); }
		swap_register(p, pgdir, PTE_ADDR(va), asid); // Register ppage for swap.
	}
	//printk("+data page: %08x, %08x -> %d\n", PTE_ADDR(va), pgdir, page2ppn(p));
}

/* Overview:
 *  Refill TLB.
 */
void _do_tlb_refill(u_long *pentrylo, u_int va, u_int asid) {
	tlb_invalidate(asid, va);
	Pte *ppte = NULL;
	/* Hints:
	 *  Invoke 'page_lookup' repeatedly in a loop to find the page table entry '*ppte'
	 * associated with the virtual address 'va' in the current address space 'cur_pgdir'.
	 *
	 *  **While** 'page_lookup' returns 'NULL', indicating that the '*ppte' could not be found,
	 *  allocate a new page using 'passive_alloc' until 'page_lookup' succeeds.
	 */

	while (page_lookup(cur_pgdir, va, &ppte) == NULL) {
		//if ((ppte != NULL) && ((*ppte) & PTE_SWAPPED)) {
			//printk("swapped out page\n");
			//swap_back(*ppte);
		//} else {
			passive_alloc(va, cur_pgdir, asid);
		//}
	}

	ppte = (Pte *)((u_long)ppte & ~0x7); // 0x7: 2 for a 32bit u_long, 1 for odd/even
	pentrylo[0] = ppte[0] >> 6;
	pentrylo[1] = ppte[1] >> 6;
}

#if !defined(LAB) || LAB >= 4
/* Overview:
 *   This is the TLB Mod exception handler in kernel.
 *   Our kernel allows user programs to handle TLB Mod exception in user mode, so we copy its
 *   context 'tf' into UXSTACK and modify the EPC to the registered user exception entry.
 *
 * Hints:
 *   'env_user_tlb_mod_entry' is the user space entry registered using
 *   'sys_set_user_tlb_mod_entry'.
 *
 *   The user entry should handle this TLB Mod exception and restore the context.
 */
void do_tlb_mod(struct Trapframe *tf) {
	// Note that we store an original version of tf in a `tmp_tf` for the following reason:
	// 
	// 1. The user's handler wants to see the original tf when the exception happens, and
	// $sp has not become a UXSTACK pointer yet(and other changes, like changing the $a0);
	//
	// 2. And the tf, lying in the KSTACK, is our interface to manage user registers, and we change
	// the user $sp to UXSTACK through writing tf.
	//
	// In other word, the tmp_tf is the actual saved scene to recover after handling
	// (by `sys_set_trapframe`).
	struct Trapframe tmp_tf = *tf;

	if (tf->regs[29] < USTACKTOP || tf->regs[29] >= UXSTACKTOP) {
		tf->regs[29] = UXSTACKTOP;
	}
	tf->regs[29] -= sizeof(struct Trapframe);
	struct Trapframe *uxstack = (struct Trapframe *)tf->regs[29];
	*uxstack = tmp_tf; // Copy the trapframe into UXSTACK

	Pte *pte;
	page_lookup(cur_pgdir, tf->cp0_badvaddr, &pte);

	if (curenv->env_user_tlb_mod_entry) {
		tf->regs[4] = tf->regs[29]; // First param is a pointer to the trapframe
		tf->regs[29] -= sizeof(tf->regs[4]);
		// Hint: Set 'cp0_epc' in the context 'tf' to 'curenv->env_user_tlb_mod_entry'.
		tf->cp0_epc = curenv->env_user_tlb_mod_entry;
	} else {
		panic("TLB Mod but no user handler registered");
	}
}
#endif
