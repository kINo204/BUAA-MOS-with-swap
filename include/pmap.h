#ifndef _PMAP_H_
#define _PMAP_H_

#include <mmu.h>
#include <printk.h>
#include <queue.h>
#include <types.h>

extern Pde *cur_pgdir;

LIST_HEAD(Page_list, Page); // Page_list type(for head of free list of physical pages)
typedef LIST_ENTRY(Page) Page_LIST_entry_t; // Page_list's node type
TAILQ_HEAD(Page_tailq, Page);
typedef TAILQ_ENTRY(Page) Page_TAILQ_entry_t;

struct Page { // physical page's management structure
	Page_LIST_entry_t pp_link;
	Page_TAILQ_entry_t swap_link;

	// Ref is the count of pointers (usually in page table entries)
	// to this page.  This only holds for pages allocated using
	// page_alloc.  Pages allocated at boot time using pmap.c's "alloc"
	// do not have valid reference count fields.
	u_short pp_ref;
};

extern struct Page *pages; // address of the page array
extern struct Page_list page_free_list; // head of the free list of physical pages

static inline u_long page2ppn(struct Page *pp) {
	return pp - pages;
}

static inline u_long page2pa(struct Page *pp) {
	return page2ppn(pp) << PGSHIFT;
}

static inline struct Page *pa2page(u_long pa) {
	if (PPN(pa) >= npage) {
		panic("pa2page called with invalid pa: %x", pa);
	}
	return &pages[PPN(pa)];
}

static inline u_long page2kva(struct Page *pp) {
	return KADDR(page2pa(pp));
}

static inline u_long va2pa(Pde *pgdir, u_long va) {
	Pte *p;

	pgdir = &pgdir[PDX(va)];
	if (!(*pgdir & PTE_V)) {
		return ~0;
	}
	p = (Pte *)KADDR(PTE_ADDR(*pgdir));
	if (!(p[PTX(va)] & PTE_V)) {
		return ~0;
	}
	return PTE_ADDR(p[PTX(va)]);
}

void mips_detect_memory(u_int _memsize);
void mips_vm_init(void);
void mips_init(u_int argc, char **argv, char **penv, u_int ram_low_size);
void page_init(void);
void *alloc(u_int n, u_int align, int clear);

int page_alloc(struct Page **pp); // allocate the 1st of free list
void page_free(struct Page *pp);
void page_decref(struct Page *pp);

/* page_insert:
* Set the page table entry(in the given page table) of the
* given virtual address to PTE(physical page, permission bits),
* and flush TLB entry with the key(virtual address, address
* space ID) by padding with 0.
*
* note: Any nonexist 2nd-level page table page will be created.
* params:
* 	- pgdir: address of the page directory
* 	- va:	 virtual address to insert at
*	- asid:  current address space ID, used for inferring TLB
*	- pp: 	 address of the physical page structure to use for the mapping
* 	- perm:  desired pattern of permission bits
*/
int page_insert(Pde *pgdir, u_int asid, struct Page *pp, u_long va, u_int perm);

/* page_lookup:
* Search in the given page table for the given virtual address, and
* return the physical page and page table entry if found.
*
* note: if the PTE is nonexist, return NULL for the address of Page,
* 		BUT the returned address of PTE may not be NULL.
* params:
* 	- pgdir: address of the page directory
* 	- va: 	 virtual address to search the page table for
*	- ppte:  OUTPUT the address of the PTE found
*/
struct Page *page_lookup(Pde *pgdir, u_long va, Pte **ppte);
void page_remove(Pde *pgdir, u_int asid, u_long va); // flush PTE(pgdir, va) and flush TLB(asid, va)

extern struct Page *pages;

void physical_memory_manage_check(void);
void page_check(void);

#endif /* _PMAP_H_ */
