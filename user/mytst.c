#include <lib.h>
int page_num = 0;
u_long va;

int main(int argc,char **argv){
	debugf("------Enter my test------\n");

	for (va = 0x500000; va < 0X7f3fd000; va += PAGE_SIZE) {
		if (va >= USTACKTOP && va < USTACKTOP + PAGE_SIZE) { continue; }
		if (va >= UENVS && va < UPAGES) { continue; }
		if (va >= UPAGES && va < UVPT) { continue; }

		*((int *) va) = 0x12345678;
		page_num++;

		// check check
		if (page_num % 5000 == 0) {
			//debugf("write va = 0x%x\n", va);
			debugf("np=%d\n", page_num);
			//debugf("first is still 0x%x\n", *((int *) 0x500000));
		}
	}

	debugf("------Finish my test-----\n");
	return 0;
}
