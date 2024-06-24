#include <lib.h>
int magic = 0x12345678;

int main(int argc,char **argv){
	debugf("------Enter my test------\n");

	write_seg(0x500000, 15000);
	if (check_seg(0x500000, 15000) == -1) { user_panic("init err"); }

	debugf("fork start\n");
	if (fork()) { // parent
		debugf("parent return from fork1\n");
		debugf("child ");
	} else {
		debugf("child return from fork1\n");
		write_seg(0x500000, 15000);
		if (check_seg(0x500000, 15000) == -1) { user_panic("init err"); }
		debugf("child ");
	}
	debugf("finished\n");


	debugf("------Finish my test-----\n");
	return 0;
}

void write_seg(u_int start, int npage) {
	for (u_int va = start; va < start + npage * PAGE_SIZE; va += PAGE_SIZE) {
		*((int *)va) = magic;
		va += 4;
		*((int *)va) = (va - start) / PAGE_SIZE;
	}
}

int check_seg(u_int start, int npage) {
	for (u_int va = start; va < start + npage * PAGE_SIZE; va += PAGE_SIZE) {
		int val = *((int *)va);
		if (val != magic) {
			debugf("wrong value read at va %08x, page #%d: %08x\n", va, (va - start) / PAGE_SIZE, val);
			return -1;
		}

		va += 4;
		val = *((int *)va);
		if (val != (va - start) / PAGE_SIZE) {
			debugf("wrong value read at va %08x, page #%d: %08x\n", va, (va - start) / PAGE_SIZE, val);
			return -1;
		}
	}
	return 0;
}
