#include <env.h>
#include <lib.h>
#include <mmu.h>

// WHY? I'm totally confused, please help me.

/* Overview:
 *   Map the faulting page to a private writable copy.
 *   ("cow" = copy on write)
 *
 * Pre-Condition:
 * 	'va' is the address which led to the TLB Mod exception.
 *
 * Post-Condition:
 *  - Launch a 'user_panic' if 'va' is not a copy-on-write page.
 *  - Otherwise, this handler should map a private writable copy of
 *    the faulting page at the same address.
 */
// Note: this function runs in USER mode!
// The param tf is a pointer to the tmp_tf's copy in UXSTACK.
static void __attribute__((noreturn)) cow_entry(struct Trapframe *tf) {
	u_int va = tf->cp0_badvaddr;
	u_int perm;
	//debugf("cow_entry: envid=%08x, va=0x%x\n", env->env_id, PTE_ADDR(va));

	/* Step 1: Find the 'perm' in which the faulting address 'va' is mapped. */
	/* Hint: Use 'vpt' and 'VPN' to find the page table entry. If the 'perm' doesn't have
	 * 'PTE_COW', launch a 'user_panic'. */
	perm = PTE_FLAGS( ((Pte*)vpt)[VPN(va)] );

	if (!((perm & PTE_V) && !(perm & PTE_SWAPPED)))
		user_panic("Error in cow_entry: invalid, not swapped in yet");
	if (!(perm & PTE_COW))
		user_panic("Error in cow_entry: not a COW page");

	/* Step 2: Remove 'PTE_COW' from the 'perm', and add 'PTE_D' to it. */
	perm &= ~PTE_COW;
	perm |= PTE_D;

	/* Step 3: Allocate a new page at 'UCOW'. */
	// UCOW's alloc help us get a new **physical page**.
	// Trying to directly use `sys_mem_alloc` will fail, because sys_mem_map->page_insert will
	// only modify the `perm` when an identical physical page is found mapped.
	try(syscall_mem_alloc(0, UCOW, perm)); // WHAT should perm here be?

	/* Step 4: Copy the content of the faulting page at 'va' to 'UCOW'. */
	/* Hint: 'va' may not be aligned to a page! */
	memcpy(UCOW, PTE_ADDR(va), PAGE_SIZE);

	// Step 5: Map the page at 'UCOW' to 'va' with the new 'perm'.
	try(syscall_mem_map(0, UCOW, 0, va, perm));

	// Step 6: Unmap the page at 'UCOW'.
	try(syscall_mem_unmap(0, UCOW));

	//debugf("END cow_entry\n");

	//perm = PTE_FLAGS(
		//		((Pte*)vpt) [VPN(va)]
			//);
	//if (perm & PTE_COW)
		//debugf("Error in cow_entry: still COW after handling\n");

	// Step 7: Return to the faulting routine.
	int r = syscall_set_trapframe(0, tf);
	user_panic("syscall_set_trapframe returned %d", r);
}

/* Overview:
 *   Grant our child 'envid' access to the virtual page 'vpn' (with address 'vpn' * 'PAGE_SIZE') in
 * our (current env's) address space. 'PTE_COW' should be used to isolate the modifications on
 * unshared memory from a parent and its children.
 *
 * Post-Condition:
 *   If the virtual page 'vpn' has 'PTE_D' and doesn't has 'PTE_LIBRARY', both our original virtual
 *   page and 'envid''s newly-mapped virtual page should be marked 'PTE_COW' and without 'PTE_D',
 *   while the other permission bits are kept.
 *
 *   If not, the newly-mapped virtual page in 'envid' should have the exact same permission as our
 *   original virtual page.
 *
 * Hint:
 *   - 'PTE_LIBRARY' indicates that the page should be shared among a parent and its children.
 *   - A page with 'PTE_LIBRARY' may have 'PTE_D' at the same time, you should handle it correctly.
 *   - You can pass '0' as an 'envid' in arguments of 'syscall_*' to indicate current env because
 *     kernel 'envid2env' converts '0' to 'curenv').
 *   - You should use 'syscall_mem_map', the user space wrapper around 'msyscall' to invoke
 *     'sys_mem_map' in kernel.
 */
static void duppage(u_int envid, u_int vpn) {
	u_int va = vpn * PAGE_SIZE;

	Pte pte = ((Pte*)vpt)[vpn];
	//if (env->env_id == 0x1802 && PTE_ADDR(va) == 0x7f3fd000) {
	//	debugf("dupping page: va=%x, pte=%x\n", PTE_ADDR(va), pte);
	//}
	u_int perm = PTE_FLAGS(pte);
	if (!(perm & PTE_V) && !(perm & PTE_SWAPPED)) { return; }
	
	if ((perm & PTE_D) && !(perm & PTE_LIBRARY) && !(perm & PTE_COW)) {
		perm &= ~PTE_D;
		perm |= PTE_COW;
		try(syscall_mem_map(0, va, envid, va, perm));
		// Modify parent page's perm.
		try(syscall_mem_map(0, va,     0, va, perm));
	} else {
		try(syscall_mem_map(0, va, envid, va, perm));
	}

	//if (env->env_id == 0x1802 && PTE_ADDR(va) == 0x7f3fd000) {
	//	debugf("end dupping\n");
	//}
}

/* Overview:
 *   User-level 'fork'. Create a child and then copy our address space.
 *   Set up ours and its TLB Mod user exception entry to 'cow_entry'.
 *
 * Post-Conditon:
 *   Child's 'env' is properly set.
 *
 * Hint:
 *   Use global symbols 'env', 'vpt' and 'vpd'.
 *   Use 'syscall_set_tlb_mod_entry', 'syscall_getenvid', 'syscall_exofork',  and 'duppage'.
 */
int fork(void) {
	if (env->env_user_tlb_mod_entry != (u_int)cow_entry) {
		try(syscall_set_tlb_mod_entry(0, cow_entry));
	}

	u_int child = syscall_exofork();
	// Only the child env, when first scheduled, will enter this condition.
	if (child == 0) {
		//debugf("change new env\n");
		env = envs + ENVX(syscall_getenvid());
		//debugf("child env generated: %x\n", env->env_id);
		// We can set $ra in trapframe to customize the child's "starting" PC for fork-exec.
		return 0;
	}

	// Parent continue here:
	for (u_int va = UTEMP; va < USTACKTOP; va += PAGE_SIZE) {
		if (vpd[PDX(va)] & PTE_V)
			duppage(child, VPN(va));
	}
	//debugf("duppages end\n");

	try(syscall_set_tlb_mod_entry(child, cow_entry));
	try(syscall_set_env_status(child, ENV_RUNNABLE));

	return child;
}
