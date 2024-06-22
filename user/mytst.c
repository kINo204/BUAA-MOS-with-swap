#include <lib.h>
int magic = 0x12345678;

int main(int argc,char **argv){
	debugf("------Enter my test------\n");
	debugf("parent start\n");
	
	write_seg(0x500000, 0x500000 + 20000 * PAGE_SIZE); // Alloc swappable pages in parent.
	debugf("writing done\n");

	debugf("parent start forking\n");
	if (fork() == 0) {	// child
		//while (1) { debugf("child run\n"); }
	} else { 			// parent
		//while (1) {}
		debugf("parent ");
	}
	debugf("return from fork\n");

	debugf("------Finish my test-----\n");
	return 0;
}

void write_seg(u_int start, u_int end) {
	for (u_int va = start; va < end; va += PAGE_SIZE) { *((int *)va) = magic; }
}
