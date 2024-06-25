#include <bitops.h>
#include <env.h>
#include <malta.h>
#include <mmu.h>
#include <pmap.h>
#include <swap.h>
#include <types.h>
#include <sdisk.h>
#include <printk.h>

/* These variables are set by mips_detect_memory(ram_low_size); */
static u_long memsize; /* Maximum physical address */
u_long npage;	       /* Amount of memory(in pages) */

Pde *cur_pgdir;

struct Page *pages;
static u_long freemem;

struct Page_list page_free_list; /* Free list of physical pages */

/* Overview:
 *   Use '_memsize' from bootloader to initialize 'memsize' and
 *   calculate the corresponding 'npage' value.
 */
void mips_detect_memory(u_int _memsize) {
	/* Step 1: Initialize memsize. */
	memsize = _memsize;

	/* Step 2: Calculate the corresponding 'npage' value. */
	/* Exercise 2.1: Your code here. */
	npage = memsize >> PGSHIFT;

	printk("Memory size: %lu KiB, number of pages: %lu\n", memsize / 1024, npage);
}

/* Lab 2 Key Code "alloc" */
/* Overview:
    Allocate `n` bytes physical memory with alignment `align`, if `clear` is set, clear the
    allocated memory.
    This allocator is used only while setting up virtual memory system.
   Post-Condition:
    If we're out of memory, should panic, else return this address of memory we have allocated.*/
void *alloc(u_int n, u_int align, int clear) {
	extern char end[];
	u_long alloced_mem;

	/* Initialize `freemem` if this is the first time. The first virtual address that the
	 * linker did *not* assign to any kernel code or global variables. */
	if (freemem == 0) {
		freemem = (u_long)end; // end
	}

	/* Step 1: Round up `freemem` up to be aligned properly */
	freemem = ROUND(freemem, align);

	/* Step 2: Save current value of `freemem` as allocated chunk. */
	alloced_mem = freemem;

	/* Step 3: Increase `freemem` to record allocation. */
	freemem = freemem + n;

	// Panic if we're out of memory.
	panic_on(PADDR(freemem) >= memsize);

	/* Step 4: Clear allocated chunk if parameter `clear` is set. */
	if (clear) {
		memset((void *)alloced_mem, 0, n);
	}

	/* Step 5: return allocated chunk. */
	return (void *)alloced_mem;
}
/* End of Key Code "alloc" */

/* Overview:
    Set up two-level page table.
   Hint:
    You can get more details about `UPAGES` and `UENVS` in include/mmu.h. */
void mips_vm_init() {
	/* Allocate proper size of physical memory for global array `pages`,
	 * for physical memory management. Then, map virtual address `UPAGES` to
	 * physical address `pages` allocated before. For consideration of alignment,
	 * you should round up the memory size before map. */
	pages = (struct Page *)alloc(npage * sizeof(struct Page), PAGE_SIZE, 1);

	printk("to memory %x for struct Pages.\n", freemem);
	printk("pmap.c:\t mips vm init success\n");
}

/* Overview:
 *   Initialize page structure and memory free list. The 'pages' array has one 'struct Page' entry
 * per physical page. Pages are reference counted, and free pages are kept on a linked list.
 *
 * Hint: Use 'LIST_INSERT_HEAD' to insert free pages to 'page_free_list'.
 */
void page_init(void) {
	swap_init();
	/* Step 1: Initialize page_free_list. */
	/* Hint: Use macro `LIST_INIT` defined in include/queue.h. */
	/* Exercise 2.3: Your code here. (1/4) */
	LIST_INIT(&page_free_list);

	/* Step 2: Align `freemem` up to multiple of PAGE_SIZE. */
	/* Exercise 2.3: Your code here. (2/4) */
	freemem = ROUND(freemem, PAGE_SIZE);

	/* Step 3: Mark all memory below `freemem` as used (set `pp_ref` to 1) */
	/* Exercise 2.3: Your code here. (3/4) */
	for (u_long pa = 0; pa < PADDR(freemem); pa += PAGE_SIZE)
	{
		pa2page(pa)->pp_ref = 1;
	}

	/* Step 4: Mark the other memory as free. */
	/* Exercise 2.3: Your code here. (4/4) */
	for (u_long pa = PADDR(freemem); pa < memsize; pa += PAGE_SIZE)
	{
		pa2page(pa)->pp_ref = 0;
		LIST_INSERT_HEAD(&page_free_list, pa2page(pa), pp_link);
	}
}

/* Overview:
 *   Allocate a physical page from free memory, and fill this page with zero.
 *
 * Post-Condition:
 *   If failed to allocate a new page (out of memory, there's no free page), return -E_NO_MEM.
 *   Otherwise, set the address of the allocated 'Page' to *pp, and return 0.
 *
 * Note:
 *   This does NOT increase the reference count 'pp_ref' of the page - the caller must do these if
 *   necessary (either explicitly or via page_insert).
 *
 * Hint: Use LIST_FIRST and LIST_REMOVE defined in include/queue.h.
 */
int page_alloc(struct Page **new) {
	/* Step 1: Get a page from free memory. If fails, return the error code.*/
	struct Page *pp;
	if (LIST_EMPTY(&page_free_list))
	{
		for (int i = 0; i < NSWAP; i++) { swap(); }
		if (LIST_EMPTY(&page_free_list)) {
			panic("swap err: no PPage available after swapping");
		} 
	}
	pp = LIST_FIRST(&page_free_list);
	LIST_REMOVE(pp, pp_link);

	/* Step 2: Initialize this page with zero. */
	memset((void*)page2kva(pp), 0, PAGE_SIZE);
	pp->swap_link.tqe_next = NULL;
	pp->swap_link.tqe_prev = NULL;
	pp->accessed = 0;

	*new = pp;
	return 0;
}

/* Overview:
 *   Release a page 'pp', mark it as free.
 *
 * Pre-Condition:
 *   'pp->pp_ref' is '0'.
 */
void page_free(struct Page *pp) {
	assert(pp->pp_ref == 0);
	/* Just insert it into 'page_free_list'. */
	/* Exercise 2.5: Your code here. */
	LIST_INSERT_HEAD(&page_free_list, pp, pp_link);
}

/* Overview:
 *   Given 'pgdir', a pointer to a page directory, 'pgdir_walk' returns a pointer to
 *   the page table entry for the page where the virtual address 'va' lies.(Offset
 * 	 field of the va will be regarded)
 *
 * Pre-Condition:
 *   'pgdir' is a two-level page table structure.
 *   'ppte' is a valid pointer, i.e., it should NOT be NULL.
 *
 * Post-Condition:
 *   If we're out of memory, return -E_NO_MEM.
 *   Otherwise, we get the page table entry, store
 *   the value of page table entry to *ppte, and return 0, indicating success.
 *
 * Hint:
 *   We use a two-level pointer to store page table entry and return a state code to indicate
 *   whether this function succeeds or not.
 */
/* VPage -> PTE(kaddr) (fixed indexed mapping) */
static int pgdir_walk(Pde *pgdir, u_long va, int create, Pte **ppte) {
	Pde *pgdir_entryp;
	struct Page *pp;

	/* Step 1: Get the corresponding page directory entry. */
	/* Exercise 2.6: Your code here. (1/3) */
	pgdir_entryp = pgdir + PDX(va);

	/* Step 2: If the corresponding page table is not existent (valid) then:
	 *   * If parameter `create` is set, create one. Set the permission bits 'PTE_C_CACHEABLE |
	 *     PTE_V' for this new page in the page directory. If failed to allocate a new page (out
	 *     of memory), return the error.
	 *   * Otherwise, assign NULL to '*ppte' and return 0.
	 */
	/* Exercise 2.6: Your code here. (2/3) */
	if (((*pgdir_entryp) & PTE_V) == 0) // invalid page directory entry, no page table yet
	{
		if (create) { // Pgtbl pages created here:
			if (page_alloc(&pp) == -E_NO_MEM) { return -E_NO_MEM; }
			pp->pp_ref += 1;
			*pgdir_entryp  = PTE_FLAGS(*pgdir_entryp);    // clear pgdir_entryp's PA field
			*pgdir_entryp |= page2pa(pp); 		// write newly allocated PA to pgdir_entryp
			*pgdir_entryp |= PTE_C_CACHEABLE | PTE_V & ~PTE_SWAPPED;
		}
		else
		{
			*ppte = NULL;
			return 0;
		}
	}

	/* Step 3: Assign the kernel virtual address of the page table entry to '*ppte'. */
	/* Exercise 2.6: Your code here. (3/3) */
	Pte* pgtbl = (Pte*)KADDR(PTE_ADDR(*pgdir_entryp));
	*ppte = pgtbl + PTX(va);
	return 0;
}

/* Overview:
 *   Map the physical page 'pp' at the page where the virtual address
 *   'va' lies. The permission (the low 12 bits) of the page table entry
 *   should be set to 'perm | PTE_C_CACHEABLE | PTE_V'.
 *
 * Post-Condition:
 *   Return 0 on success
 *   Return -E_NO_MEM, if page table couldn't be allocated
 *
 * Hint:
 *   If there is already a page mapped at `va`, call page_remove() to release this mapping.
 *   The `pp_ref` should be incremented if the insertion succeeds.
 */
/* Modify mapping: VPage -> PPage (configurable by editing PTE)  */
int page_insert(Pde *pgdir, u_int asid, struct Page *pp, u_long va, u_int perm) {
	Pte *pte;

	/* Step 1: Get PTE. */
	pgdir_walk(pgdir, va, 0, &pte);

	// Guarantee the ORIGINAL VPage is in physical mem.
	if (pte && !(*pte & PTE_V) && (*pte & PTE_SWAPPED)) {
		swap_back(*pte);
	}

	// A valid pte exist
	if (pte && (*pte & PTE_V)) {
		// Different physical page
		if (pa2page(*pte) != pp) {
			page_remove(pgdir, asid, va);
		} else { // Same physical page
			tlb_invalidate(asid, va);
			*pte = page2pa(pp) | (perm & ~PTE_SWAPPED) | PTE_C_CACHEABLE | PTE_V;
			return 0;
		}
	}

	/* Step 2: Flush TLB with 'tlb_invalidate'. */
	tlb_invalidate(asid, va);

	/* Step 3: Re-get or create the page table entry. */
	/* If failed to create, return the error. */
	if (pgdir_walk(pgdir, va, 1, &pte) == -E_NO_MEM) {
		return -E_NO_MEM;
	}

	/* Step 4: Insert the page to the page table entry with 'perm | PTE_C_CACHEABLE | PTE_V'
	 * and increase its 'pp_ref'. */
	*pte = page2pa(pp) | (perm & ~PTE_SWAPPED) | PTE_C_CACHEABLE | PTE_V;
	pp->pp_ref++;
	pp->accessed = 1;

	return 0;
}

/* Lab 2 Key Code "page_lookup" */
/*Overview:
    Look up the Page that the virtual page where `va` lies map to.
  Post-Condition:
    Return a pointer to corresponding Page, and store it's page table entry to *ppte.
    If `va` doesn't mapped to any Page, return NULL.*/
/* Look up mapping: VPage -> PPage */
struct Page *page_lookup(Pde *pgdir, u_long va, /* for efficiency */ Pte **ppte) {
	struct Page *pp;
	Pte *pte;

	/* Step 1: Get the page table entry. */
	pgdir_walk(pgdir, va, 0, &pte);

	// Swap back if data is on disk.
	if (pte && !(*pte & PTE_V) && (*pte & PTE_SWAPPED)) {
		swap_back(*pte);
	}

	/* Hint: Check if the page table entry doesn't exist or is not valid. */
	if (pte == NULL || (*pte & PTE_V) == 0) {
		if (ppte) {
			*ppte = pte;
		}
		return NULL;
	}

	/* Step 2: Get the corresponding Page struct. */
	/* Hint: Use function `pa2page`, defined in include/pmap.h . */
	pp = pa2page(*pte);
	if (ppte) {
		*ppte = pte;
	}
	pp->accessed = 1;

	return pp;
}
/* End of Key Code "age_lookup" */

/* Overview:
 *   Decrease the 'pp_ref' value of Page 'pp'.
 *   When there's no references (mapped virtual address) to this page, release it.
 */
void page_decref(struct Page *pp) {
	assert(pp->pp_ref > 0);

	/* If 'pp_ref' reaches to 0, free this page. */
	if (--pp->pp_ref == 0) {
		page_free(pp);
	}
}

/* Lab 2 Key Code "page_remove" */
// Overview:
//   Unmap the physical page at virtual address 'va'.
/* Modify mapping(removal): VPage -> PPage */
void page_remove(Pde *pgdir, u_int asid, u_long va) {
	Pte *pte;

	/* Step 1: Get the page table entry, and check if the page table entry is valid. */
	struct Page *pp = page_lookup(pgdir, va, &pte);
	if (pp == NULL) { return; } // invalid VPage

	/* Step 2: Decrease reference count on 'pp'. */
	//if (pp->pp_ref == 1) { printk("-data page: %08x, %08x -> %d\n", PTE_ADDR(va), pgdir, page2ppn(pp)); }
	page_decref(pp);

	// Unregister the pp in swap_tbl.
	swap_unregister(pp, pgdir, va, asid);

	/* Step 3: Flush TLB. */
	*pte = 0; // PTE set to INVALID at the time.
	tlb_invalidate(asid, va);
	return;
}
/* End of Key Code "page_remove" */

void physical_memory_manage_check(void) {
	struct Page *pp, *pp0, *pp1, *pp2;
	struct Page_list fl;
	int *temp;

	// should be able to allocate three pages
	pp0 = pp1 = pp2 = 0;
	assert(page_alloc(&pp0) == 0);
	assert(page_alloc(&pp1) == 0);
	assert(page_alloc(&pp2) == 0);

	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);

	// temporarily steal the rest of the free pages
	fl = page_free_list;
	// now this page_free list must be empty!!!!
	LIST_INIT(&page_free_list);
	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	temp = (int *)page2kva(pp0);
	// write 1000 to pp0
	*temp = 1000;
	// free pp0
	page_free(pp0);
	printk("The number in address temp is %d\n", *temp);

	// alloc again
	assert(page_alloc(&pp0) == 0);
	assert(pp0);

	// pp0 should not change
	assert(temp == (int *)page2kva(pp0));
	// pp0 should be zero
	assert(*temp == 0);

	page_free_list = fl;
	page_free(pp0);
	page_free(pp1);
	page_free(pp2);
	struct Page_list test_free;
	struct Page *test_pages;
	test_pages = (struct Page *)alloc(10 * sizeof(struct Page), PAGE_SIZE, 1);
	LIST_INIT(&test_free);
	// LIST_FIRST(&test_free) = &test_pages[0];
	int i, j = 0;
	struct Page *p, *q;
	for (i = 9; i >= 0; i--) {
		test_pages[i].pp_ref = i;
		// test_pages[i].pp_link=NULL;
		// printk("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);
		LIST_INSERT_HEAD(&test_free, &test_pages[i], pp_link);
		// printk("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);
	}
	p = LIST_FIRST(&test_free);
	int answer1[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	assert(p != NULL);
	while (p != NULL) {
		// printk("%d %d\n",p->pp_ref,answer1[j]);
		assert(p->pp_ref == answer1[j++]);
		// printk("ptr: 0x%x v: %d\n",(p->pp_link).le_next,((p->pp_link).le_next)->pp_ref);
		p = LIST_NEXT(p, pp_link);
	}
	// insert_after test
	int answer2[] = {0, 1, 2, 3, 4, 20, 5, 6, 7, 8, 9};
	q = (struct Page *)alloc(sizeof(struct Page), PAGE_SIZE, 1);
	q->pp_ref = 20;

	// printk("---%d\n",test_pages[4].pp_ref);
	LIST_INSERT_AFTER(&test_pages[4], q, pp_link);
	// printk("---%d\n",LIST_NEXT(&test_pages[4],pp_link)->pp_ref);
	p = LIST_FIRST(&test_free);
	j = 0;
	// printk("into test\n");
	while (p != NULL) {
		//      printk("%d %d\n",p->pp_ref,answer2[j]);
		assert(p->pp_ref == answer2[j++]);
		p = LIST_NEXT(p, pp_link);
	}

	printk("physical_memory_manage_check() succeeded\n");
}

void page_check(void) {
	struct Page *pp, *pp0, *pp1, *pp2;
	struct Page_list fl;

	// should be able to allocate a page for directory
	assert(page_alloc(&pp) == 0);
	Pde *boot_pgdir = (Pde *)page2kva(pp);

	// should be able to allocate three pages
	pp0 = pp1 = pp2 = 0;
	assert(page_alloc(&pp0) == 0);
	assert(page_alloc(&pp1) == 0);
	assert(page_alloc(&pp2) == 0);

	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);

	// temporarily steal the rest of the free pages
	fl = page_free_list;
	// now this page_free list must be empty!!!!
	LIST_INIT(&page_free_list);

	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	// there is no free memory, so we can't allocate a page table
	assert(page_insert(boot_pgdir, 0, pp1, 0x0, 0) < 0);

	// free pp0 and try again: pp0 should be used for page table
	page_free(pp0);
	assert(page_insert(boot_pgdir, 0, pp1, 0x0, 0) == 0);
	assert(PTE_FLAGS(boot_pgdir[0]) == (PTE_C_CACHEABLE | PTE_V));
	assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
	assert(PTE_FLAGS(*(Pte *)page2kva(pp0)) == (PTE_C_CACHEABLE | PTE_V));

	printk("va2pa(boot_pgdir, 0x0) is %x\n", va2pa(boot_pgdir, 0x0));
	printk("page2pa(pp1) is %x\n", page2pa(pp1));
	//  printk("pp1->pp_ref is %d\n",pp1->pp_ref);
	assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
	assert(pp1->pp_ref == 1);

	// should be able to map pp2 at PAGE_SIZE because pp0 is already allocated for page table
	assert(page_insert(boot_pgdir, 0, pp2, PAGE_SIZE, 0) == 0);
	assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp2));
	assert(pp2->pp_ref == 1);

	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	printk("start page_insert\n");
	// should be able to map pp2 at PAGE_SIZE because it's already there
	assert(page_insert(boot_pgdir, 0, pp2, PAGE_SIZE, 0) == 0);
	assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp2));
	assert(pp2->pp_ref == 1);

	// pp2 should NOT be on the free list
	// could happen in ref counts are handled sloppily in page_insert
	assert(page_alloc(&pp) == -E_NO_MEM);

	// should not be able to map at PDMAP because need free page for page table
	assert(page_insert(boot_pgdir, 0, pp0, PDMAP, 0) < 0);

	// insert pp1 at PAGE_SIZE (replacing pp2)
	assert(page_insert(boot_pgdir, 0, pp1, PAGE_SIZE, 0) == 0);

	// should have pp1 at both 0 and PAGE_SIZE, pp2 nowhere, ...
	assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
	assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp1));
	// ... and ref counts should reflect this
	assert(pp1->pp_ref == 2);
	printk("pp2->pp_ref %d\n", pp2->pp_ref);
	assert(pp2->pp_ref == 0);
	printk("end page_insert\n");

	// pp2 should be returned by page_alloc
	assert(page_alloc(&pp) == 0 && pp == pp2);

	// unmapping pp1 at 0 should keep pp1 at PAGE_SIZE
	page_remove(boot_pgdir, 0, 0x0);
	assert(va2pa(boot_pgdir, 0x0) == ~0);
	assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp1));
	assert(pp1->pp_ref == 1);
	assert(pp2->pp_ref == 0);

	// unmapping pp1 at PAGE_SIZE should free it
	page_remove(boot_pgdir, 0, PAGE_SIZE);
	assert(va2pa(boot_pgdir, 0x0) == ~0);
	assert(va2pa(boot_pgdir, PAGE_SIZE) == ~0);
	assert(pp1->pp_ref == 0);
	assert(pp2->pp_ref == 0);

	// so it should be returned by page_alloc
	assert(page_alloc(&pp) == 0 && pp == pp1);

	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	// forcibly take pp0 back
	assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
	boot_pgdir[0] = 0;
	assert(pp0->pp_ref == 1);
	pp0->pp_ref = 0;

	// give free list back
	page_free_list = fl;

	// free the pages we took
	page_free(pp0);
	page_free(pp1);
	page_free(pp2);
	page_free(pa2page(PADDR(boot_pgdir)));

	printk("page_check() succeeded!\n");
}

/* swap.c
 */


struct Page_tailq page_swap_queue;

// Swap table: index table of PPage -> PTE
//
// `swap_tbl` is the starting addr of an SwapTableEntry array,
// indexed by PPN.
// Each SwapTableEntry is a LIST_HEAD item for a list of registered
// VPage, whose info is recorded in struct SwapInfo.
// When a VPage of swappable type is allocated with a PPage, a SwapInfo
// will be registered through swap_register().
// Later when trying to swap out the page, all corresponding SwapInfos
// will be traversed to refill the page tables, unsetting V and setting
// software flag SWAPPED, as well as writing disk address in the PTE.
SwapTableEntry *swap_tbl;
SwapTableEntry *bno_tbl;//[SD_NBLK];

struct SwapInfo swapInfos[MAX_SWAPINFO];
LIST_HEAD(, SwapInfo) swapInfo_free_list;

void swap_init(void) {
	// Init swap disk bitmap.
	sd_bitmap_init();

	// page_swap_queue
	TAILQ_INIT(&page_swap_queue);

	// swap_tbl, swapInfos, swapInfo_free_list
	swap_tbl = (SwapTableEntry *)alloc(npage * sizeof(SwapTableEntry), PAGE_SIZE, 1);
	bno_tbl = (SwapTableEntry *)alloc(SD_NBLK * sizeof(SwapTableEntry), PAGE_SIZE, 1);
	//swapInfos = (struct SwapInfo *)alloc(MAX_SWAPINFO * sizeof(struct SwapInfo), PAGE_SIZE, 1);
	printk("to memory %x for struct SwapInfos.\n", freemem);
	LIST_INIT(&swapInfo_free_list);
	for (int i = 0; i < MAX_SWAPINFO; i++) {
		LIST_INSERT_HEAD(&swapInfo_free_list, &swapInfos[i], link);
	}
}

/* Register a SwapInfo in the corresponding ste in swap_tbl.
   Called when a VPage of swappable type is accessed, requiring a PPage.
 */
void swap_register(struct Page *pp, Pde *pgdir, u_int va, u_int asid) {
	/*if ((PTE_ADDR(va) == 0x60000000 || PTE_ADDR(va) == UCOW)
			&& (curenv->env_id == 0x1802 || curenv->env_id == 0x2802)
			) {
	//if (PTE_ADDR(va) == 0x7f3ff000) {
		printk("register pgdir=%x, ppn=%d, va=0x%08x, num=%d\n", pgdir, page2ppn(pp), PTE_ADDR(va), pp->pp_ref);
	}*/

	// If it's the first time the PPage is mapped, insert it to swap_queue.
	SwapTableEntry *pp_ste = page2ste(pp);
	if (LIST_EMPTY(pp_ste)) {
		TAILQ_INSERT_TAIL(&page_swap_queue, pp, swap_link);
	}

	// Get a free SwapInfo and edit it.
	if (LIST_EMPTY(&swapInfo_free_list)) { panic("no struct SwapInfo available"); }
	struct SwapInfo *sinfo = LIST_FIRST(&swapInfo_free_list);
	LIST_REMOVE(sinfo, link);
	sinfo->pgdir = pgdir;
	sinfo->va    = PTE_ADDR(va);
	sinfo->asid  = asid;

	// Insert sinfo to corresponding list of pp.
	SwapTableEntry *ste = page2ste(pp);
	LIST_INSERT_HEAD(ste, sinfo, link);
}

void swap_unregister(struct Page *pp, Pde *pgdir, u_int va, u_int asid) {
	// Find the corresponding SwapInfo from the swap_tbl and remove it.
	/*if ((PTE_ADDR(va) == 0x60000000 || PTE_ADDR(va) == UCOW)
			&& (curenv->env_id == 0x1802 || curenv->env_id == 0x2802)
			) {
	//if (PTE_ADDR(va) == 0x7f3ff000) {
	//if (curenv->env_id == 0x1802) {
		printk("unregister pgdir=%x ppn=%d, va=0x%08x, num=%d\n", pgdir, page2ppn(pp), PTE_ADDR(va), pp->pp_ref);
	}*/

	struct SwapInfo *sinfo;
	SwapTableEntry *ste = page2ste(pp);
	if (LIST_EMPTY(ste)) { return; }

	LIST_FOREACH(sinfo, ste, link) {
		if ((((u_int) sinfo->pgdir) == ((u_int) pgdir))
				&& (sinfo->asid == asid)
				&& (PTE_ADDR(sinfo->va) == PTE_ADDR(va))) {
			LIST_REMOVE(sinfo, link);
			LIST_INSERT_HEAD(&swapInfo_free_list, sinfo, link);
			break;
		}
	}

	if (LIST_EMPTY(ste)) {
		if (pp->swap_link.tqe_next || pp->swap_link.tqe_prev) {
			TAILQ_REMOVE(&page_swap_queue, pp, swap_link); // Remove the PPage from swappable queue.
			pp->swap_link.tqe_next = NULL;
			pp->swap_link.tqe_prev = NULL;
		}
	}
}

void swap_back(Pte cur_pte) {
	//printk("back\n");
	// Allocate a page.
	struct Page *p;
	panic_on(page_alloc(&p));

	// Recover swapped data from the disk block.
	u_int sd_bno = cur_pte >> PGSHIFT;
	panic_on(sd_bno >= SD_NBLK);
	read_page(p, sd_bno);
	sd_block_free(sd_bno);

	// For all VPage of this swapped page, recover:
	SwapTableEntry *bno_ste = bno2ste(sd_bno);
	struct SwapInfo *sinfo;
	assert(p->pp_ref == 0);
	LIST_FOREACH(sinfo, bno_ste, link) {
		// Recover PTE.
		Pde pde = sinfo->pgdir[PDX(sinfo->va)];
		Pte *pte = (Pte *)KADDR( PTE_ADDR(pde) ) + PTX(sinfo->va);

		panic_on(*pte & PTE_V);
		panic_on(!(*pte & PTE_SWAPPED));

		*pte |= PTE_V;
		*pte &= ~PTE_SWAPPED;
		*pte = PTE_FLAGS(*pte);
		*pte |= PTE_ADDR(page2pa(p));

		//tlb_invalidate(sinfo->asid, sinfo->va); // Invalidate corresponding TLB entry.
		/*if ((sinfo->va == 0x60000000) && (curenv->env_id == 0x2802 || curenv->env_id == 0x1802)) {
			printk("in:  ");
			_print_sinfo(sinfo); 
		}*/

		p->pp_ref++;
}
	TAILQ_INSERT_TAIL(&page_swap_queue, p, swap_link);

	// Move SwapInfo from bno_ste to page_ste.
	SwapTableEntry *ste = page2ste(p);
	while (!LIST_EMPTY(bno_ste)) {
		sinfo = LIST_FIRST(bno_ste);
		LIST_REMOVE(sinfo, link);
		LIST_INSERT_HEAD(ste, sinfo, link);
	}
	//printk("end back\n");
}

// Pick a page in memory and swap it out to the swapping disk.
void swap(void) {
	// Get a PPage from page_swap_queue.
	if (TAILQ_EMPTY(&page_swap_queue)) { return;/*panic("no swappable page");*/ }

	// Second-chance Clock.
	static struct Page *last_next = NULL;
	if (!last_next) { last_next = TAILQ_FIRST(&page_swap_queue); }
	struct Page *pp = last_next;
	int max = 100;
	while (1) {
		if (TAILQ_NEXT(pp, swap_link)) {
			pp = TAILQ_NEXT(pp, swap_link);
		} else { // Jump to the first on the end.
			pp = TAILQ_FIRST(&page_swap_queue);
		}
		if (pp == last_next) { break; } // We came back after a full circle.
		if (pp->accessed == 1) {
			pp->accessed = 0;
		} else {
			if (--max <= 0) { break; }
		}
	}
	last_next = (TAILQ_NEXT(pp, swap_link) != NULL) ?
		TAILQ_NEXT(pp, swap_link) : TAILQ_FIRST(&page_swap_queue);

	// Write PPage data to a disk block.
	int sd_bno = sd_block_alloc();
	panic_on(sd_bno >= SD_NBLK);
	write_page(pp, sd_bno);
	//printk("page #0x%d data swapped to disk\n", page2ppn(pp));

	// Refresh the PTE of all VPage mapping the swapped PPage and flush all TLB entries.
	struct SwapInfo *sinfo;
	SwapTableEntry *ste = page2ste(pp);
	LIST_FOREACH(sinfo, ste, link) {
		Pde pde = sinfo->pgdir[PDX(sinfo->va)];
		Pte *pte = (Pte *)KADDR( PTE_ADDR(pde) ) + PTX(sinfo->va);

		panic_on(!(*pte & PTE_V));
		panic_on(*pte & PTE_SWAPPED);

		*pte &= ~PTE_V;  	 		// Unset V.
		*pte |= PTE_SWAPPED; 		// Set soft-flag SWAPPED.
		*pte = PTE_FLAGS(*pte); 	// Clear PTE's PAddr field.
		*pte |= PTE_ADDR(sd_bno << PGSHIFT); // Set addr to sd_bno.

		tlb_invalidate(sinfo->asid, sinfo->va); // Invalidate corresponding TLB entry.

		/*if ((sinfo->va == 0x60000000) && (curenv->env_id == 0x2802 || curenv->env_id == 0x1802)) {
			printk("out: ");
			_print_sinfo(sinfo);
		}*/

		pp->pp_ref--;
	}

	if (pp->pp_ref != 0) {
		printk("pp_ref=%d\n", pp->pp_ref);
		LIST_FOREACH(sinfo, ste, link) { _print_sinfo(sinfo); }
	}
	panic_on(pp->pp_ref != 0);

	// Move sinfos from swap_tbl to bno_tbl.
	SwapTableEntry *bno_ste = bno2ste(sd_bno);
	while (!LIST_EMPTY(ste)) {
		sinfo = LIST_FIRST(ste);
		LIST_REMOVE(sinfo, link);
		LIST_INSERT_HEAD(bno_ste, sinfo, link);
	}

	// Free the PPage.

	TAILQ_REMOVE(&page_swap_queue, pp, swap_link);
	pp->swap_link.tqe_next = NULL;
	pp->swap_link.tqe_prev = NULL;

	LIST_INSERT_HEAD(&page_free_list, pp, pp_link);

	//printk("end swap\n");
}

void _print_sinfo(struct SwapInfo *sinfo) {
	if (!sinfo) {
		printk("SwapInfo: null\n"); 
		return;
	}

	Pde pde = sinfo->pgdir[PDX(sinfo->va)];
	Pte *pte = (Pte *)KADDR( PTE_ADDR(pde) ) + PTX(sinfo->va);
	printk(
			"SwapInfo:\t"
			"vaddr=%x  "
			"pgdir=%x  "
			"pte=%x\n"
			, sinfo->va, sinfo->pgdir, *pte
		  );
}


/* sdisk.c
 * Operations on swapping disk.
 * This is a CPU friendly PIO SWAP driver.
 */

int sd_bitmap[SD_NBLK / 32 + 1]; // Bits unset on free.

int sd_block_alloc() {
	int bno;
	for (bno = 0; bno < SD_NBLK; bno++) {
		if ((sd_bitmap[bno / 32] & (1 << (bno % 32))) == 0) {
			sd_bitmap[bno / 32] |= (1 << (bno % 32));
			if (bno >= 0xfffff) { panic("sd_bno too big"); }
			return bno;
		}
	}
	panic("swap disk out of space");
}

void sd_block_free(u_int bno) {
	sd_bitmap[bno / 32] &= ~(1 << (bno % 32));
}

void sd_bitmap_init() {
	// Clear sd_bitmap to 0.
	for (int i = 0; i < SD_NBLK / 32 + 1; i++) {
		sd_bitmap[i] = 0;
	}
}

static int read_sd(u_int va, u_int pa, u_int len) {
	// Check length, only byte, half-word and word.
	if (len != 1 && len != 2 && len != 4) { return -E_INVAL; }
	// Check pa.
	int valid_pa = 0;
	valid_pa |= (0x18000170 <= pa && pa + len <= 0x18000170 + 0x8) ? 1 : 0; // swap disk
	if (valid_pa == 0) { return -E_INVAL; };

	// Perform read and return.
	memcpy((void*)va, (void*)(KSEG1 + pa), (size_t)len);
	return 0;
}

static int write_sd(u_int va, u_int pa, u_int len) {
	// Check length, only byte, half-word and word.
	if (len != 1 && len != 2 && len != 4) { return -E_INVAL; }
	// Check pa.
	int valid_pa = 0;
	valid_pa |= (0x18000170 <= pa && pa + len <= 0x18000170 + 0x8) ? 1 : 0; // swap disk
	if (valid_pa == 0) { return -E_INVAL; };

	// Perform write and return.
	memcpy((void*)(KSEG1 + pa), (void*)va, (size_t)len);
	return 0;
}

// TODO hard polling!
static uint8_t wait_sd_ready() {
	uint8_t flag;
	while (1) {
		panic_on(read_sd(&flag, MALTA_SWAP_STATUS, 1));
		if ((flag & MALTA_SWAP_BUSY) == 0) {
			break;
		}
	}
	return flag;
}

void sd_read(u_int diskno, u_int secno, void *dst, u_int nsecs) {
	uint8_t temp; // Alloc a main mem BUFFER on the stack.
	// `offset` records the starting offset of the next sector to operate.
	u_int offset = 0, max = nsecs + secno;
	panic_on(diskno != 2);

	while (secno < max) {
		temp = wait_sd_ready();
		// Step 1: Write the number of operating sectors to NSECT register
		temp = 1;
		panic_on(write_sd(&temp, MALTA_SWAP_NSECT, 1));
		// Step 2: Write the 7:0 bits of sector number to LBAL register
		temp = secno & 0xff;
		panic_on(write_sd(&temp, MALTA_SWAP_LBAL, 1));
		// Step 3: Write the 15:8 bits of sector number to LBAM register
		temp = (secno >> 8) & 0xff;
		panic_on(write_sd(&temp, MALTA_SWAP_LBAM, 1));
		// Step 4: Write the 23:16 bits of sector number to LBAH register
		temp = (secno >> 16) & 0xff;
		panic_on(write_sd(&temp, MALTA_SWAP_LBAH, 1));
		// Step 5: Write the 27:24 bits of sector number, addressing mode
		// and diskno to DEVICE register
		temp = ((secno >> 24) & 0x0f) | MALTA_SWAP_LBA | (diskno << 4);
		panic_on(write_sd(&temp, MALTA_SWAP_DEVICE, 1));
		// Step 6: Write the working mode to STATUS register
		temp = MALTA_SWAP_CMD_PIO_READ;
		panic_on(write_sd(&temp, MALTA_SWAP_STATUS, 1));

		// Step 7: Wait until the SWAP is ready
		temp = wait_sd_ready();
		// Step 8: Read the data from device
		for (int i = 0; i < SECT_SIZE / 4; i++) {
			panic_on(read_sd(dst + offset + i * 4, MALTA_SWAP_DATA, 4));
		}
		// Step 9: Check SWAP status
		panic_on(read_sd(&temp, MALTA_SWAP_STATUS, 1));

		offset += SECT_SIZE;
		secno += 1;
	}
}

void sd_write(u_int diskno, u_int secno, void *src, u_int nsecs) {
	uint8_t temp;
	u_int offset = 0, max = nsecs + secno;
	panic_on(diskno != 2);

	// Write the sector in turn
	while (secno < max) {
		temp = wait_sd_ready();
		// Step 1: Write the number of operating sectors to NSECT register
		temp = 1;
		panic_on(write_sd(&temp, MALTA_SWAP_NSECT, 1));
		// Step 2: Write the 7:0 bits of sector number to LBAL register
		temp = secno & 0xff;
		panic_on(write_sd(&temp, MALTA_SWAP_LBAL, 1));
		// Step 3: Write the 15:8 bits of sector number to LBAM register
		temp = (secno >> 8) & 0xff;
		panic_on(write_sd(&temp, MALTA_SWAP_LBAM, 1));
		// Step 4: Write the 23:16 bits of sector number to LBAH register
		temp = (secno >> 16) & 0xff;
		panic_on(write_sd(&temp, MALTA_SWAP_LBAH, 1));
		// Step 5: Write the 27:24 bits of sector number, addressing mode
		// and diskno to DEVICE register
		temp = ((secno >> 24) & 0x0f) | MALTA_SWAP_LBA | (diskno << 4);
		panic_on(write_sd(&temp, MALTA_SWAP_DEVICE, 1));
		// Step 6: Write the working mode to STATUS register
		temp = MALTA_SWAP_CMD_PIO_WRITE;
		panic_on(write_sd(&temp, MALTA_SWAP_STATUS, 1));
		// Step 7: Wait until the SWAP is ready
		temp = wait_sd_ready();
		// Step 8: Write the data to device
		for (int i = 0; i < SECT_SIZE / 4; i++) {
			/* Exercise 5.3: Your code here. (9/9) */
			panic_on(write_sd(src + offset + i * 4, MALTA_SWAP_DATA, 4));
		}
		// Step 9: Check SWAP status
		panic_on(read_sd(&temp, MALTA_SWAP_STATUS, 1));

		offset += SECT_SIZE;
		secno += 1;
	}
}

void write_page(struct Page *pp, u_int bno) {
	u_long kva = page2kva(pp);
	sd_write(2, bno * SECT2BLK, kva, SECT2BLK);
	//printk("value 1st byte to bno %x: %x\n", bno, *((int *)kva));
}

void read_page(struct Page *pp, u_int bno) {
	u_long kva = page2kva(pp);
	sd_read(2, bno * SECT2BLK, kva, SECT2BLK);
	//printk("value 1st byte at bno %x: %x\n", bno, *((int *)kva));
}

void test_sdisk() {
	printk("Testing sdisk...\n");
	int magic = 0x12345678;
	struct Page *p;
	u_long base;

	// Fill a page with magic num.
	page_alloc(&p);
	base = page2kva(p);
	for (u_long ofs = 0; ofs < PAGE_SIZE; ofs += 4) {
		*((int *)(base + ofs)) = magic;
	}
	printk("Finish writing the page\n");

	// Write the content of the page to block 5.
	write_page(p, 5);
	page_free(p); // Free the page.
	printk("Finish storing the page\n");

	// Reallocate a page and read the disk to check.
	page_alloc(&p);
	base = page2kva(p);
	read_page(p, 5);
	for (u_long ofs = 0; ofs < PAGE_SIZE; ofs += 4) {
		int value = *((int *)(base + ofs));
		if (value != magic) {
			panic("sdisk test failed, wrong value read");
		} //else {
			//printk("read value: 0x%x\n", value);
		//}
	}
	printk("Finish reading the page\n");
	printk("Test sdisk finished!\n");
}
