#include <lib.h>
int magic = 0x12345678;

int main(int argc,char **argv){
	debugf("------Enter my test------\n");

	write_seg(0x500000, 8000);
	debugf("fork1\n");
	fork();
	debugf("fork2\n");
	fork();
	debugf("fork3\n");
	fork();
	debugf("fork4\n");
	fork();
	debugf("fork5\n");
	fork();
	debugf("fork6\n");
	fork();

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
