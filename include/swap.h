#ifndef _SWAP_H_
#define _SWAP_H_

#include <pmap.h>
#include <sdisk.h>

#define MAX_SWAPINFO 0x15000

extern struct Page_tailq page_swap_queue;

typedef LIST_ENTRY(SwapInfo) SwapInfoLink;
struct SwapInfo {
	SwapInfoLink link;
	Pde *pgdir;
	u_int asid;
	u_int va; // The VA that triggers page allocation
			  // when this page is first allocated.
			  // Enough for finding PTE and flush TLB.
};
typedef LIST_HEAD(SwapTableEntry_t, SwapInfo) SwapTableEntry;
extern SwapTableEntry *swap_tbl;
extern SwapTableEntry *bno_tbl;//[SD_NBLK];

static inline SwapTableEntry *page2ste(struct Page *pp) {
	return swap_tbl + (pp - pages);
}

static inline SwapTableEntry *bno2ste(u_int sd_bno) {
	if (sd_bno < 0) { return NULL; }
	return bno_tbl + sd_bno;
}

void swap_init(void);
void swap_register(struct Page *pp, Pde *pgdir, u_int va, u_int asid);
void swap_unregister(struct Page *pp, Pde *pgdir, u_int va, u_int asid);
void swap(void);
void swap_back(Pte cur_pte);

#endif
