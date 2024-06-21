/*
 * Operations on swapping disk.
 * This is a CPU friendly PIO SWAP driver.
 */

#include <sdisk.h>
#include <malta.h>
#include <mmu.h>
#include <pmap.h>

int sd_bitmap[SD_NBLK / 32 + 1]; // Bits unset on free.

int sd_block_alloc() {
	int bno;
	for (bno = 0; bno < SD_NBLK; bno++) {
		if ((sd_bitmap[bno / 32] & (1 << (bno % 32))) == 0) {
			sd_bitmap[bno / 32] |= (1 << (bno % 32));
			if (bno >= 0xfffff) {
				panic("sd_bno too big");
			}
			return bno;
		}
	}
	panic("swap disk out of space");
}

void sd_block_free(u_int bno) {
	sd_bitmap[bno / 32] &= ~(1 << (bno % 32));
}

void sd_bitmap_init() {
	for (int i = 0; i < SD_NBLK / 32 + 1; i++) {
		sd_bitmap[i] = 0;
	}
}

static int read_sd(u_int va, u_int pa, u_int len) {
	// Check length.
	if (len != 1 && len != 2 && len != 4) { return -E_INVAL; }
	// Check pa.
	int valid_pa = 0;
	valid_pa |= (0x18000170 <= pa && pa + len <= 0x18000170 + 0x8)  ? 1 : 0; // swap disk
	if (valid_pa == 0) { return -E_INVAL; };

	// Perform write and return.
	memcpy((void*)va, (void*)(KSEG1 + pa), (size_t)len);
	return 0;
}

static int write_sd(u_int va, u_int pa, u_int len) {
	// Check length, only byte, half-word and word.
	if (len != 1 && len != 2 && len != 4) { return -E_INVAL; }
	// Check pa.
	int valid_pa = 0;
	valid_pa |= (0x18000170 <= pa && pa + len <= 0x18000170 + 0x8)  ? 1 : 0; // swap disk
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
}

void read_page(struct Page *pp, u_int bno) {
	u_long kva = page2kva(pp);
	sd_read(2, bno * SECT2BLK, kva, SECT2BLK);
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
