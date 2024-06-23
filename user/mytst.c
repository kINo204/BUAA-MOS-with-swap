#include <lib.h>
int magic = 0x12345678;

int main(int argc,char **argv){
	debugf("------Enter my test------\n");

	write_seg(0x500000, 20000);
	if (fork()) { // parent
		write_seg(0x500000, 20000);
		debugf("parent ");
	} else {
		write_seg(0x500000, 20000);
		debugf("child ");
	}
	debugf("return from fork\n");

	debugf("------Finish my test-----\n");
	return 0;
}

void write_seg(u_int start, int npage) {
	for (u_int va = start; va < start + npage * PAGE_SIZE; va += PAGE_SIZE) { *((int *)va) = magic; }
}
