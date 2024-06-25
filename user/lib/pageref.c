#include <lib.h>

/* Overview:
 *   Get the reference count for the physical page mapped at the given address.
 *
 * Post-Condition:
 *   Return the number of virtual pages mapped to this physical page among all envs.
 *   if the virtual address is valid.
 *   Otherwise return 0.
 *
 * Hint:
 *   Use 'vpd' and 'vpt'.
 */
int pageref(void *v) {
	u_int pte;

	/* Step 1: Check the page directory. */
	if (!(vpd[PDX(v)] & PTE_V)) {
		return 0;
	}

	/* Step 2: Check the page table. */
	//int tmp = *((int *)v); // Trigger swapping back
	pte = vpt[VPN(v)];
	sys_mem_map(0, v, 0, v, PTE_FLAGS(pte)); // Trigger swapping back
	pte = vpt[VPN(v)];
	if (!(pte & PTE_V)) { return 0; }
	/* Step 3: Return the result. */
	return pages[PPN(pte)].pp_ref;
}
