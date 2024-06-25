#include <lib.h>
int magic = 0x12345678;
int num_page = 16000;

void recv_seg(u_int start, int num_page) {
	for (u_int va = start; va < start + num_page * PAGE_SIZE; va += PAGE_SIZE) {
		ipc_recv(NULL, (void *)va, NULL);
	}
}

void send_seg(u_int start, int num_page, int envid) {
	for (u_int va = start; va < start + num_page * PAGE_SIZE; va += PAGE_SIZE) {
		ipc_send(envid, 1, va, 0);
	}
}

void write_seg(u_int start, int num_page) {
	for (u_int va = start; va < start + num_page * PAGE_SIZE; va += PAGE_SIZE) {
		*((int *)va) = magic;
		va += 4;
		*((int *)va) = (va - start) / PAGE_SIZE;
		va -= 4;
	}
}

int check_seg(u_int start, int num_page) {
	for (u_int va = start; va < start + num_page * PAGE_SIZE; va += PAGE_SIZE) {
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
		va -= 4;
	}
	return 0;
}

int main(int argc,char **argv){
	debugf("------Enter my test------\n");

	int child = fork();
	if (child == 0) { // child
		recv_seg(0x608000, num_page);
		check_seg(0x608000, num_page);
		debugf("child done\n");
	} else { // parent
		write_seg(0x500000, num_page);
		send_seg(0x500000, num_page, child);
		debugf("parent send done\n");
	}

	debugf("------Finish my test-----\n");
	return 0;
}
