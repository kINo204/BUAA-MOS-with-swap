#include <env.h>
#include <io.h>
#include <mmu.h>
#include <pmap.h>
#include <swap.h>
#include <printk.h>
#include <sched.h>
#include <syscall.h>

extern struct Env *curenv;

/* Overview:
 * 	This function is used to print a character on screen.
 *
 * Pre-Condition:
 * 	`c` is the character you want to print.
 */
void sys_putchar(int c) {
	printcharc((char)c);
	return;
}

/* Overview:
 * 	This function is used to print a string of bytes on screen.
 *
 * Pre-Condition:
 * 	`s` is base address of the string, and `num` is length of the string.
 */
int sys_print_cons(const void *s, u_int num) {
	if (((u_int)s + num) > UTOP || ((u_int)s) >= UTOP || (s > s + num)) {
		return -E_INVAL;
	}
	u_int i;
	for (i = 0; i < num; i++) {
		printcharc(((char *)s)[i]);
	}
	return 0;
}

/* Overview:
 *	This function provides the environment id of current process.
 *
 * Post-Condition:
 * 	return the current environment id
 */
u_int sys_getenvid(void) {
	return curenv->env_id;
}

/* Overview:
 *   Give up remaining CPU time slice for 'curenv'.
 *
 * Post-Condition:
 *   Another env is scheduled.
 *
 * Hint:
 *   This function will never return.
 */
// void sys_yield(void);
void __attribute__((noreturn)) sys_yield(void) {
	// Hint: Just use 'schedule' with 'yield' set.
	schedule(1);
}

/* Overview:
 * 	Destroy the current environment.
 *
 * Pre-Condition:
 * 	The parameter `envid` must be the environment id of a
 * process, which is either a child of the caller of this function
 * or the caller itself.
 *
 * Post-Condition:
 *  Returns 0 on success.
 *  Returns the original error if underlying calls fail.
 */
int sys_env_destroy(u_int envid) {
	struct Env *e;
	try(envid2env(envid, &e, 1));

	printk("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

/* Overview:
 *   Register the entry of user space TLB Mod handler of 'envid'.
 *
 * Post-Condition:
 *   The 'envid''s TLB Mod exception handler entry will be set to 'func'.
 *   Returns 0 on success.
 *   Returns the original error if underlying calls fail.
 */
int sys_set_tlb_mod_entry(u_int envid, u_int func) {
	struct Env *env;
	/* Step 1: Convert the envid to its corresponding 'struct Env *' using 'envid2env'. */
	try(envid2env(envid, &env, 1));
	/* Step 2: Set its 'env_user_tlb_mod_entry' to 'func'. */
	env->env_user_tlb_mod_entry = func;
	return 0;
}

/* Overview:
 *   Check 'va' is illegal or not, according to include/mmu.h
 */
static inline int is_illegal_va(u_long va) {
	// UVPT not allowed.
	return va < UTEMP || va >= UTOP;
}

static inline int is_illegal_va_range(u_long va, u_int len) {
	if (len == 0) {
		return 0;
	}
	return va + len < va || va < UTEMP || va + len > UTOP;
}

/* Overview:
 *   Allocate a physical page and map 'va' to it with 'perm' in the address space of 'envid'.
 *   If 'va' is already mapped, that original page is sliently unmapped.
 *   'envid2env' should be used with 'checkperm' set, like in most syscalls, to ensure the target is
 * either the caller or its child.
 *
 * Post-Condition:
 *   Return 0 on success.
 *   Return -E_BAD_ENV: 'checkperm' of 'envid2env' fails for 'envid'.
 *   Return -E_INVAL:   'va' is illegal (should be checked using 'is_illegal_va').
 *   Return the original error: underlying calls fail (you can use 'try' macro).
 *
 * Hint:
 *   You may want to use the following functions:
 *   'envid2env', 'page_alloc', 'page_insert', 'try' (macro)
 */
int sys_mem_alloc(u_int envid, u_int va, u_int perm) {
	struct Env *env;
	struct Page *pp;

	/* Step 1: Check if 'va' is a legal user virtual address using 'is_illegal_va'. */
	if (is_illegal_va(va)) { return -E_INVAL; }

	/* Step 2: Convert the envid to its corresponding 'struct Env *' using 'envid2env'. */
	/* Hint: **Always** validate the permission in syscalls! */
	try(envid2env(envid, &env, 1));
	
	/* Step 3: Allocate a physical page using 'page_alloc'. */
	try(page_alloc(&pp));

	/* Step 4: Map the allocated page at 'va' with permission 'perm' using 'page_insert'. */
	//perm &= ~PTE_SWAPPED;
	int r = page_insert(env->env_pgdir, env->env_asid, pp, va, perm);
	//printk("+data page: %08x, %08x -> %d\n", PTE_ADDR(va), env->env_pgdir, page2ppn(pp));

	// TODO Should syscall_mem_alloc() pages be swappable?
	if (1) { // condition not sure for now
		panic_on(pp == NULL);
		swap_register(pp, env->env_pgdir, va, env->env_asid); // Set page swappable
	}

	return r;
}

/* Overview:
 *   Find the physical page mapped at 'srcva' in the address space of env 'srcid', and map 'dstid''s
 *   'dstva' to it with 'perm'.
 *
 * Post-Condition:
 *   Return 0 on success.
 *   Return -E_BAD_ENV: 'checkperm' of 'envid2env' fails for 'srcid' or 'dstid'.
 *   Return -E_INVAL: 'srcva' or 'dstva' is illegal, or 'srcva' is unmapped in 'srcid'.
 *   Return the original error: underlying calls fail.
 *
 * Hint:
 *   You may want to use the following functions:
 *   'envid2env', 'page_lookup', 'page_insert'
 */
/* Let the src and dst env share a PPage.
 * Former mapping: src_VPage -> PPage
 * After mapping:  src_VPage -> PPage, dst_VPage -> PPage
 */
int sys_mem_map(u_int srcid, u_int srcva, u_int dstid, u_int dstva, u_int perm) {
	struct Env *srcenv;
	struct Env *dstenv;
	//printk("sys_mem_map: dstid %x\n", dstid);
	///if (PTE_ADDR(srcva) == 0x531f000 && dstid == 0x1802) {
		//printk("mem_map ustack to 0x1802\n");
	//}

	if (is_illegal_va(srcva) || is_illegal_va(dstva)) { return -E_INVAL; }
	try(envid2env(srcid, &srcenv, 1));
	try(envid2env(dstid, &dstenv, 1));


	// Maintain swappable attribute of ???.
	struct Page *orgp = page_lookup(dstenv->env_pgdir, dstva, NULL);
	int swappable_org = 0;
	if (orgp != NULL) { swappable_org = !LIST_EMPTY(page2ste(orgp)); }

	/* Step 4: Find the physical page mapped at 'srcva' in the address space of 'srcid'. */
	/* Return -E_INVAL if 'srcva' is not mapped. */
	Pte *pte;
	struct Page *srcp = page_lookup(srcenv->env_pgdir, srcva, &pte);
	if (srcp == NULL) { return -E_INVAL; };
	int swappable_src = !LIST_EMPTY(page2ste(srcp));

	//if (srcva == 0x7f3fd000 && dstid == 0x2803) { printk("origin page: pgdir=%x, pte=%x, ppn=%d\n", srcenv->env_pgdir, *pte, page2ppn(srcp)); }

	/* Step 5: Map the physical page at 'dstva' in the address space of 'dstid'. */
	// Note that the swap_unregister needed lies in the page_remove in this page_insert call.
	//perm &= ~PTE_SWAPPED;
	int r = page_insert(dstenv->env_pgdir, dstenv->env_asid, srcp, dstva, perm);

	//struct Page *dstp = page_lookup(dstenv->env_pgdir, dstva, &pte);
	//panic_on(srcp != dstp);
	//if (srcva == 0x7f3fd000 && dstid == 0x2803) { printk("mapped page: pgdir=%x, pte=%x, ppn=%d\n", dstenv->env_pgdir, *pte, page2ppn(dstp)); }

	// Register new SwapInfo for VPage.
	int swappable = srcva == UCOW ? swappable_org : swappable_src;
	if (swappable  // the original page should be swappable
			&& ((srcid != dstid) || (PTE_ADDR(srcva) != PTE_ADDR(dstva)))) {
		swap_register(srcp, dstenv->env_pgdir, dstva, dstenv->env_asid);
	}

	//if (PTE_ADDR(srcva) == 0x531f000 && dstid == 0x1802) {
		//printk("end mem_map ustack to 0x1802\n");
	//}
	return r;
}

/* Overview:
 *   Unmap the physical page mapped at 'va' in the address space of 'envid'.
 *   If no physical page is mapped there, this function silently succeeds.
 *
 * Post-Condition:
 *   Return 0 on success.
 *   Return -E_BAD_ENV: 'checkperm' of 'envid2env' fails for 'envid'.
 *   Return -E_INVAL:   'va' is illegal.
 *   Return the original error when underlying calls fail.
 */
int sys_mem_unmap(u_int envid, u_int va) {
	struct Env *e;
	/* Step 1: Check if 'va' is a legal user virtual address using 'is_illegal_va'. */
	if (is_illegal_va(va)) { return -E_INVAL; }

	/* Step 2: Convert the envid to its corresponding 'struct Env *' using 'envid2env'. */
	try(envid2env(envid, &e, 1));

	/* Step 3: Unmap the physical page at 'va' in the address space of 'envid'. */
	page_remove(e->env_pgdir, e->env_asid, va);
	return 0;
}

/* Overview:
 *   Allocate a new env as a child of 'curenv'.
 *
 * Post-Condition:
 *   Returns the child's envid on success, and
 *   - The new env's 'env_tf' is copied from the kernel stack, except for $v0 set to 0 to indicate
 *     the return value in child.
 *   - The new env's 'env_status' is set to 'ENV_NOT_RUNNABLE'.
 *   - The new env's 'env_pri' is copied from 'curenv'.
 *   Returns the original error if underlying calls fail.
 *
 * Hint:
 *   This syscall works as an essential step in user-space 'fork' and 'spawn'.
 */
int sys_exofork(void) {
	struct Env *e;

	/* Step 1: Allocate a new env using 'env_alloc'. */
	try(env_alloc(&e, curenv->env_id)); // WHY alloc or create

	/* Step 2: Copy the current Trapframe below 'KSTACKTOP' to the new env's 'env_tf'. */
	// We're using KSTACKTOP(the newest tf) instead of curenv->tf because we want the `fork`
	// process to happen exactly here, and the current tf under KSTACKTOP has already been
	// modified, thus different from the curenv->tf saved when entering exception.
	e->env_tf = *((struct Trapframe*)KSTACKTOP - 1);
	//if (e->env_id == 0x2803)
		//printk("epc=%08x, ra=%08x\n", e->env_tf.cp0_epc, e->env_tf.regs[31]);

	/* Step 3: Set the new env's 'env_tf.regs[2]' to 0 to indicate the return value in child. */
	e->env_tf.regs[2] = 0; // $v0 = 0

	/* Step 4: Set up the new env's 'env_status' and 'env_pri'.  */
	e->env_status 	= ENV_NOT_RUNNABLE; // WHY not runnable?
	e->env_pri		= curenv->env_pri;  // WHY same priority?

	return e->env_id;
}

/* Overview:
 *   Set 'envid''s 'env_status' to 'status' and update 'env_sched_list'.
 *
 * Post-Condition:
 *   Returns 0 on success.
 *   Returns -E_INVAL if 'status' is neither 'ENV_RUNNABLE' nor 'ENV_NOT_RUNNABLE'.
 *   Returns the original error if underlying calls fail.
 *
 * Hint:
 *   The invariant that 'env_sched_list' contains and only contains all runnable envs should be
 *   maintained.
 */
int sys_set_env_status(u_int envid, u_int status) {
	struct Env *env;
	/* Step 1: Check if 'status' is valid. */
	if (!(status == ENV_RUNNABLE || status == ENV_NOT_RUNNABLE))
	{
		return -E_INVAL;
	}
	/* Step 2: Convert the envid to its corresponding 'struct Env *' using 'envid2env'. */
	try(envid2env(envid, &env, 1));
	/* Step 3: Update 'env_sched_list' if the 'env_status' of 'env' is being changed. */
	if (env->env_status != status)
	{
		if (status == ENV_RUNNABLE)
		{
			TAILQ_INSERT_HEAD(&env_sched_list, env, env_sched_link);
		}
		else // ENV_NOT_RUNNABLE
		{
			TAILQ_REMOVE(&env_sched_list, env, env_sched_link);
		}
	}
	/* Step 4: Set the 'env_status' of 'env'. */
	env->env_status = status;
	return 0;
}

/* Overview:
 *  Set envid's trap frame to 'tf'.
 *
 * Post-Condition:
 *  The target env's context is set to 'tf'.
 *  Returns 0 on success (except when the 'envid' is the current env, so no value could be
 * returned).
 *  Returns -E_INVAL if the environment cannot be manipulated or 'tf' is invalid.
 *  Returns the original error if other underlying calls fail.
 */
int sys_set_trapframe(u_int envid, struct Trapframe *tf) {
	if (is_illegal_va_range((u_long)tf, sizeof *tf)) {
		return -E_INVAL;
	}
	struct Env *env;
	try(envid2env(envid, &env, 1));
	if (env == curenv) {
		*((struct Trapframe *)KSTACKTOP - 1) = *tf;
		// Return `tf->regs[2]` instead of 0, because return value overrides regs[2] on
		// current trapframe.
		return tf->regs[2];
	} else {
		env->env_tf = *tf;
		return 0;
	}
}

/* Overview:
 * 	Kernel panic with message `msg`.
 *
 * Post-Condition:
 * 	This function will halt the system.
 */
void sys_panic(char *msg) {
	panic("%s", TRUP(msg));
}

/* Overview:
 *   Wait for a message (a value, together with a page if 'dstva' is not 0) from other envs.
 *   'curenv' is blocked until a message is sent.
 *
 * Post-Condition:
 *   Return 0 on success.
 *   Return -E_INVAL: 'dstva' is neither 0 nor a legal address.
 */
// Enter IPC receiving mode, set up receiving configs and sleep.
// Return -E_INVAL on error; no return if success(yield)
int sys_ipc_recv(u_int dstva) {
	/* Step 1: Check if 'dstva' is either zero or a legal address. */
	if (dstva != 0 && is_illegal_va(dstva)) { return -E_INVAL; }

	/* Step 2: Set 'curenv->env_ipc_recving' to 1. */
	curenv->env_ipc_recving = 1;

	/* Step 3: Set the value of 'curenv->env_ipc_dstva'. */
	curenv->env_ipc_dstva = dstva;

	/* Step 4: Set the status of 'curenv' to 'ENV_NOT_RUNNABLE' and remove it from
	 * 'env_sched_list'. */
	curenv->env_status = ENV_NOT_RUNNABLE;
	TAILQ_REMOVE(&env_sched_list, curenv, env_sched_link);

	/* Step 5: Give up the CPU and block until a message is received. */
	// Set the return value of the syscall(success=0).
	// We're successful here, and will jump to schedule(noreturn),
	// so the wrapper "do_syscall" won't be able to set the return value
	// in the trapframe for us, and we have to do it here.
	((struct Trapframe *)KSTACKTOP - 1)->regs[2] = 0; // $v0 = 0

	schedule(1); // yield
}

/* Overview:
 *   Try to send a 'value' (together with a page if 'srcva' is not 0) to the target env 'envid'.
 *
 * Post-Condition:
 *   Return 0 on success, and the target env is updated as follows:
 *   - 'env_ipc_recving' is set to 0 to block future sends.
 *   - 'env_ipc_from' is set to the sender's envid.
 *   - 'env_ipc_value' is set to the 'value'.
 *   - 'env_status' is set to 'ENV_RUNNABLE' again to recover from 'ipc_recv'.
 *   - if 'srcva' is not NULL, map 'env_ipc_dstva' to the same page mapped at 'srcva' in 'curenv'
 *     with 'perm'.
 *
 *   Return -E_IPC_NOT_RECV if the target has not been waiting for an IPC message with
 *   'sys_ipc_recv'.
 *   Return the original error when underlying calls fail.
 */
int sys_ipc_try_send(u_int envid, u_int value, u_int srcva, u_int perm) {
	struct Env *e; // target env
	struct Page *p;

	if (srcva != 0 && is_illegal_va(srcva)) { return -E_INVAL; }
	try(envid2env(envid, &e, 0));

	/* Step 3: Check if the target is waiting for a message. */
	/* Exercise 4.8: Your code here. (6/8) */
	if (e->env_ipc_recving == 0) { return -E_IPC_NOT_RECV; }

	/* Step 4: Set the target's ipc fields. */
	e->env_ipc_value = value;
	e->env_ipc_from = curenv->env_id;
	e->env_ipc_perm = PTE_V | perm & ~PTE_SWAPPED;
	e->env_ipc_recving = 0;

	/* Step 5: Set the target's status to 'ENV_RUNNABLE' again and insert it to the tail of
	 * 'env_sched_list'. */
	/* Exercise 4.8: Your code here. (7/8) */
	e->env_status = ENV_RUNNABLE;
	TAILQ_INSERT_TAIL(&env_sched_list, e, env_sched_link);

	/* Step 6: If 'srcva' is not zero, map the page at 'srcva' in 'curenv' to 'e->env_ipc_dstva'
	 * in 'e'. */
	/* Return -E_INVAL if 'srcva' is not zero and not mapped in 'curenv'. */
	if (srcva != 0) {
		/* Exercise 4.8: Your code here. (8/8) */
		p = page_lookup(curenv->env_pgdir, srcva, NULL);
		if (p == NULL) { return -E_INVAL; }
		int swappable = !LIST_EMPTY(page2ste(p));

		page_insert(e->env_pgdir, e->env_asid, p, e->env_ipc_dstva, perm);
		if (swappable
				&& !((e == curenv) && (PTE_ADDR(e->env_ipc_dstva) == PTE_ADDR(srcva)))
				) {
			swap_register(p, e->env_pgdir, e->env_ipc_dstva, e->env_asid);
		}
	}
	return 0;
}

// XXX: kernel does busy waiting here, blocking all envs
int sys_cgetc(void) {
	int ch;
	while ((ch = scancharc()) == 0) {
	}
	return ch;
}

/* Overview:
 *  This function is used to write data at 'va' with length 'len' to a device physical address
 *  'pa'. Remember to check the validity of 'va' and 'pa' (see Hint below);
 *
 *  'va' is the starting address of source data, 'len' is the
 *  length of data (in bytes), 'pa' is the physical address of
 *  the device (maybe with a offset).
 *
 * Pre-Condition:
 *  'len' must be 1, 2 or 4, otherwise return -E_INVAL.
 *
 * Post-Condition:
 *  Data within [va, va+len) is copied to the physical address 'pa'.
 *  Return 0 on success.
 *  Return -E_INVAL on bad address.
 *
 * Hint:
 *  You can use 'is_illegal_va_range' to validate 'va'.
 *  You may use the unmapped and uncached segment in kernel address space (KSEG1)
 *  to perform MMIO by assigning a corresponding-lengthed data to the address,
 *  or you can just simply use the io function defined in 'include/io.h',
 *  such as 'iowrite32', 'iowrite16' and 'iowrite8'.
 *
 *  All valid device and their physical address ranges:
 *	* ---------------------------------*
 *	|   device   | start addr | length |
 *	* -----------+------------+--------*
 *	|  console   | 0x180003f8 | 0x20   |
 *	|  IDE disk  | 0x180001f0 | 0x8    |
 *	* ---------------------------------*
 */
int sys_write_dev(u_int va, u_int pa, u_int len) {
	/* Exercise 5.1: Your code here. (1/2) */
	// Check length, only byte, half-word and word.
	if (len != 1 && len != 2 && len != 4) { return -E_INVAL; }
	// Check va.
	if (is_illegal_va_range(va, len)) { return -E_INVAL; }
	// Check pa.
	int valid_pa = 0;
	valid_pa |= (0x180003f8 <= pa && pa + len <= 0x180003f8 + 0x20) ? 1 : 0; // console
	valid_pa |= (0x180001f0 <= pa && pa + len <= 0x180001f0 + 0x8)  ? 1 : 0; // IDE disk
	if (valid_pa == 0) { return -E_INVAL; };

	// Perform write and return.
	memcpy((void*)(KSEG1 + pa), (void*)va, (size_t)len);
	return 0;
}

/* Overview:
 *  This function is used to read data from a device physical address.
 *
 * Pre-Condition:
 *  'len' must be 1, 2 or 4, otherwise return -E_INVAL.
 *
 * Post-Condition:
 *  Data at 'pa' is copied from device to [va, va+len).
 *  Return 0 on success.
 *  Return -E_INVAL on bad address.
 *
 * Hint:
 *  You can use 'is_illegal_va_range' to validate 'va'.
 *  You can use function 'ioread32', 'ioread16' and 'ioread8' to read data from device.
 */
int sys_read_dev(u_int va, u_int pa, u_int len) {
	/* Exercise 5.1: Your code here. (2/2) */
	// Check length.
	if (len != 1 && len != 2 && len != 4) { return -E_INVAL; }
	// Check va.
	if (is_illegal_va_range(va, len)) { return -E_INVAL; }
	// Check pa.
	int valid_pa = 0;
	valid_pa |= (0x180003f8 <= pa && pa + len <= 0x180003f8 + 0x20) ? 1 : 0; // console
	valid_pa |= (0x180001f0 <= pa && pa + len <= 0x180001f0 + 0x8)  ? 1 : 0; // IDE disk
	if (valid_pa == 0) { return -E_INVAL; };

	// Perform write and return.
	memcpy((void*)va, (void*)(KSEG1 + pa), (size_t)len);
	return 0;
}

void *syscall_table[MAX_SYSNO] = {
    [SYS_putchar] = sys_putchar,
    [SYS_print_cons] = sys_print_cons,
    [SYS_getenvid] = sys_getenvid,
    [SYS_yield] = sys_yield,
    [SYS_env_destroy] = sys_env_destroy,
    [SYS_set_tlb_mod_entry] = sys_set_tlb_mod_entry,
    [SYS_mem_alloc] = sys_mem_alloc,
    [SYS_mem_map] = sys_mem_map,
    [SYS_mem_unmap] = sys_mem_unmap,
    [SYS_exofork] = sys_exofork,
    [SYS_set_env_status] = sys_set_env_status,
    [SYS_set_trapframe] = sys_set_trapframe,
    [SYS_panic] = sys_panic,
    [SYS_ipc_try_send] = sys_ipc_try_send,
    [SYS_ipc_recv] = sys_ipc_recv,
    [SYS_cgetc] = sys_cgetc,
    [SYS_write_dev] = sys_write_dev,
    [SYS_read_dev] = sys_read_dev,
};

/* Overview:
 *   Call the function in 'syscall_table' indexed at 'sysno' with arguments from user context and
 * stack.
 *
 * Hint:
 *   Use sysno from $a0 to dispatch the syscall.
 *   The possible arguments are stored at $a1, $a2, $a3, [$sp + 16 bytes], [$sp + 20 bytes] in
 *   order.
 *   Number of arguments cannot exceed 5.
 */
void do_syscall(struct Trapframe *tf) {
	int (*func)(u_int, u_int, u_int, u_int, u_int);
	int sysno = tf->regs[4]; // syscall number in $a0
	// check for invalid sysno
	if (sysno < 0 || sysno >= MAX_SYSNO) {
		tf->regs[2] = -E_NO_SYS;
		return;
	}
	/* Step 1: Add the EPC in 'tf' by a word (size of an instruction). */
	tf->cp0_epc += 4;
	/* Step 2: Use 'sysno' to get 'func' from 'syscall_table'. */
	func = syscall_table[sysno];
	/* Step 3: First 3 args are stored in $a1, $a2, $a3. */
	u_int arg1 = tf->regs[5];
	u_int arg2 = tf->regs[6];
	u_int arg3 = tf->regs[7];
	/* Step 4: Last 2 args are stored in stack at [$sp + 16 bytes], [$sp + 20 bytes]. */
	// Don't use $sp; it's a kernel stack pointer now.
	u_int arg4, arg5;
	arg4 = *((u_int*)(tf->regs[29] + 16));
	arg5 = *((u_int*)(tf->regs[29] + 20));
	/* Step 5: Invoke 'func' with retrieved arguments and store its return value to $v0 in 'tf'.
	 */
	// User get the return value of syscall through $v0, so set tf->regs[2]
	// and $v0 will be set when RESTORE_ALL.
	// This only works for the "func"s who return to here. For noreturn "func"s, they can also
	// set the trapframe themselves.
	tf->regs[2] = func(arg1, arg2, arg3, arg4, arg5); // $v0 = func( ... );
}
