#include <swap.h>
#include <pmap.h>
#include <types.h>
#include <sdisk.h>

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
SwapTableEntry *bno_tbl;

#define MAX_SWAPINFO 0x10000

struct SwapInfo swapInfos[MAX_SWAPINFO];
LIST_HEAD(, SwapInfo) swapInfo_free_list;

void swap_init(void) {
	// Init swap disk bitmap.
	sd_bitmap_init();

	// page_swap_queue
	TAILQ_INIT(&page_swap_queue);

	// swap_tbl, swapInfos, swapInfo_free_list
	swap_tbl = (SwapTableEntry *)alloc(npage * sizeof(SwapTableEntry), PAGE_SIZE, 1);
	bno_tbl = (SwapTableEntry *)alloc((SD_MAX / BLOCK_SIZE) * sizeof(SwapTableEntry), PAGE_SIZE, 1);
	LIST_INIT(&swapInfo_free_list);
	for (int i = 0; i < MAX_SWAPINFO; i++) {
		LIST_INSERT_HEAD(&swapInfo_free_list, &swapInfos[i], link);
	}
}

/* Register a SwapInfo in the corresponding ste in swap_tbl.
   Called when a VPage of swappable type is accessed, requiring a PPage.
 */
void swap_register(struct Page *pp, Pde *pgdir, u_int va, u_int asid) {
	if (PTE_ADDR(va) == 0x7f3fd000
	//if (PTE_ADDR(va) == 0x7f3ff000
			&& curenv->env_id == 0x1802
			) {
		printk("reg: ppn %d  va %08x  num_aft %d", page2ppn(pp), PTE_ADDR(va), pp->pp_ref);
	}
	// If it's the first time the PPage is mapped, insert it to swap_queue.
	SwapTableEntry *pp_ste = page2ste(pp);
	if (LIST_EMPTY(pp_ste)) {
		TAILQ_INSERT_TAIL(&page_swap_queue, pp, swap_link); // Add the PPage to swappable queue.
	}

	// Get a free SwapInfo and edit it.
	if (LIST_EMPTY(&swapInfo_free_list)) { panic("no struct SwapInfo available"); }
	struct SwapInfo *sinfo = LIST_FIRST(&swapInfo_free_list);
	LIST_REMOVE(sinfo, link);
	sinfo->pgdir = pgdir;
	sinfo->va    = va;
	sinfo->asid  = asid;

	// Insert sinfo to corresponding list of pp.
	SwapTableEntry *ste = page2ste(pp);
	LIST_INSERT_HEAD(ste, sinfo, link);
}

void swap_unregister(struct Page *pp, Pde *pgdir, u_int va, u_int asid) {
	if (PTE_ADDR(va) == 0x7f3fd000
	//if (PTE_ADDR(va) == 0x7f3ff000
			&& curenv->env_id == 0x1802
			) {
		printk("reg: ppn %d  va %08x  num_aft %d", page2ppn(pp), PTE_ADDR(va), pp->pp_ref);
	}
	// Find the corresponding SwapInfo from the swap_tbl and remove it.
	struct SwapInfo *sinfo;
	SwapTableEntry *ste = page2ste(pp);
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
		if (pp->swap_link.tqe_next) {
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
	panic_on(sd_bno >= 0xfffff);
	read_page(p, sd_bno);
	sd_block_free(sd_bno);

	// For all VPage of this swapped page, recover:
	SwapTableEntry *bno_ste = bno2ste(sd_bno);
	struct SwapInfo *sinfo;
	LIST_FOREACH(sinfo, bno_ste, link) {
		// Recover PTE.
		Pde pde = sinfo->pgdir[PDX(sinfo->va)];
		Pte *pte = (Pte *)KADDR( PTE_ADDR(pde) ) + PTX(sinfo->va);

		*pte |= PTE_V;
		*pte &= ~PTE_SWAPPED;
		*pte = PTE_FLAGS(*pte);
		*pte |= PTE_ADDR(page2pa(p));

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
	//printk("swap\n");

	// Get a PPage from page_swap_queue.
	if (TAILQ_EMPTY(&page_swap_queue)) { panic("no swappable page"); }
	struct Page *pp = TAILQ_FIRST(&page_swap_queue);

	// Write PPage data to a disk block.
	int sd_bno = sd_block_alloc();
	panic_on(sd_bno >= SD_NBLK);
	write_page(pp, sd_bno);
	//printk("page #0x%d data swapped to disk\n", page2ppn(pp));

	// Refresh the PTE of all VPage mapping the swapped PPage and flush all TLB entries.
	struct SwapInfo *sinfo;
	SwapTableEntry *ste = page2ste(pp);
	LIST_FOREACH(sinfo, ste, link){
		Pde pde = sinfo->pgdir[PDX(sinfo->va)];
		Pte *pte = (Pte *)KADDR( PTE_ADDR(pde) ) + PTX(sinfo->va);

		//panic_on(!(*pte & PTE_V));
		panic_on(*pte & PTE_SWAPPED);

		*pte &= ~PTE_V;  	 		// Unset V.
		*pte |= PTE_SWAPPED; 		// Set soft-flag SWAPPED.
		*pte = PTE_FLAGS(*pte); 	// Clear PTE's PAddr field.
		*pte |= PTE_ADDR(sd_bno << PGSHIFT); // Set addr to sd_bno.
		tlb_invalidate(sinfo->asid, sinfo->va); // Invalidate corresponding TLB entry.
	}

	// Move sinfos from swap_tbl to bno_tbl.
	SwapTableEntry *bno_ste = bno2ste(sd_bno);
	while (!LIST_EMPTY(ste)) {
		sinfo = LIST_FIRST(ste);
		LIST_REMOVE(sinfo, link);
		LIST_INSERT_HEAD(bno_ste, sinfo, link);
	}

	// Free the PPage.
	pp->pp_ref = 0;

	TAILQ_REMOVE(&page_swap_queue, pp, swap_link);
	pp->swap_link.tqe_next = NULL;
	pp->swap_link.tqe_prev = NULL;

	LIST_INSERT_HEAD(&page_free_list, pp, pp_link);

	//printk("end swap\n");
}
