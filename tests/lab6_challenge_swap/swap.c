#include <lib.h>

int main() {
	// Write to memory.
    int cnt = 16384 - 512;
    debugf("[swap test] writing to memory...\n");
    for (int i = 0; i < cnt; i++) {
        u_int va = 0x44000000 + (i << 12) + (i * 4 % 4096);
		//debugf("[swap test] writing to va 0x%x\n", va);
        *(int *)va = i;
    }
    debugf("[swap test] done\n");

    debugf("[swap test] reading from memory...\n");
    for (int i = 0; i < cnt; i++) {
        u_int va = 0x44000000 + (i << 12) + (i * 4 % 4096);
		//debugf("reading va: 0x%x\n", va);
        int v = *(int *)va;
        if (i != v) {
            debugf("failed on page %d, va=0x%x\n", i, va);
            debugf("%d expected but %d found\n", i, v);
			debugf("PTE = %x\n", vpt[i]);
            panic_on(i != v);
        }
    }
    debugf("[swap test] swap test ok\n");
    int r;
    syscall_read_dev(&r, 0x10000010, 4);
    return 0;
}
