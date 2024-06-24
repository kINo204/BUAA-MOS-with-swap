#ifndef _SDISK_H_
#define _SDISK_H_

#include <pmap.h>
#include <types.h>

#define SECT_SIZE 512
#define SD_NBLK /*1024*/131072
#define SD_MAX (SD_NBLK * BLOCK_SIZE)
// Restriction: PA in PTE is 20 bits, max 0xFFFFF.
#define SECT2BLK (BLOCK_SIZE / SECT_SIZE)
/* sectors to a block */
#define BLOCK_SIZE PAGE_SIZE

// Swap disk bitmap control.
int sd_block_alloc();
void sd_block_free();
void sd_bitmap_init();

// Swap disk access control.
void test_sdisk(void); // simple test
void read_page(struct Page *pp, u_int bno);
void write_page(struct Page *pp, u_int bno);

#endif
