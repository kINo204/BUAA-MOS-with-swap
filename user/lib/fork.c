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

	/* Step 1: Find the 'perm' in which the faulting address 'va' is mapped. */
	/* Hint: Use 'vpt' and 'VPN' to find the page table entry. If the 'perm' doesn't have
	 * 'PTE_COW', launch a 'user_panic'. */
	perm = PTE_FLAGS(
				((Pte*)vpt) [VPN(va)]
			);

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
	memcpy(UCOW, va - (va % PAGE_SIZE), PAGE_SIZE); // A substitution of ROUNDDOWN, maybe ...

	// Step 5: Map the page at 'UCOW' to 'va' with the new 'perm'.
	try(syscall_mem_map(0, UCOW, 0, va, perm));

	// Step 6: Unmap the page at 'UCOW'.
	try(syscall_mem_unmap(0, UCOW));

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
	int r;
	u_int addr;
	u_int perm;

	/* Step 1: Get the permission of the page. */
	/* Hint: Use 'vpt' to find the page table entry. */
	Pte pte = ((Pte*)vpt)[vpn];
	perm = PTE_FLAGS(pte);
	// No need to duplicate invalid page(no actual physical page here)
	if (!(perm & PTE_V)) {
		return;
	} // not sure

	/* Step 2: If the page is writable, and not shared with children, and not marked as COW yet,
	 * then map it as copy-on-write, both in the parent (0) and the child (envid). */
	/* Hint: The page should be first mapped to the child before remapped in the parent. (Why?)
	 */
	addr = vpn * PAGE_SIZE;
	if ((perm & PTE_D) && !(perm & PTE_LIBRARY) && !(perm & PTE_COW))
	{
		// Both: unset D(writable), set COW
		try(syscall_mem_map(0, addr, envid, addr, perm & ~PTE_D | PTE_COW)); // Map child
		try(syscall_mem_map(0, addr,     0, addr, perm & ~PTE_D | PTE_COW)); // Remap parent
	}
	else
	{
		try(syscall_mem_map(0, addr, envid, addr, perm)); // Map child
	}
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
	u_int child; // envid of the child process
	u_int i;

	/* Step 1: Set our TLB Mod user exception entry to 'cow_entry' if not done yet. */
	if (env->env_user_tlb_mod_entry != (u_int)cow_entry) {
		try(syscall_set_tlb_mod_entry(0, cow_entry));
	}

	/* Step 2: Create a child env that's not ready to be scheduled. */
	// Hint: 'env' should always point to the current env itself, so we should fix it to the
	// correct value.
	child = syscall_exofork();
	// Only the child env, when first scheduled, will enter(actually start from) this condition.
	if (child == 0) {
		// Initialize our env structure.
		env = envs + ENVX(syscall_getenvid());
		// We can set $ra in trapframe to customize the child's "starting" PC for fork-exec.
		return 0;
	}

	/* Step 3: Map all MAPPED pages below 'USTACKTOP' into the child's address space. */
	// Hint: You should use 'duppage'.
	for (u_int va = UTEMP; va < USTACKTOP; va += PAGE_SIZE)
	{
		duppage(child, VPN(va));
	}

	/* Step 4: Set up the child's tlb mod handler and set child's 'env_status' to
	 * 'ENV_RUNNABLE'. */
	/* Hint:
	 *   You may use 'syscall_set_tlb_mod_entry' and 'syscall_set_env_status'
	 *   Child's TLB Mod user exception entry should handle COW, so set it to 'cow_entry'
	 */
	try(syscall_set_tlb_mod_entry(child, cow_entry));
	try(syscall_set_env_status(child, ENV_RUNNABLE));

	return child;
}
