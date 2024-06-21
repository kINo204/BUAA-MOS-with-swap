#include <lib.h>
#define N 16
int v[1 << N];
u_int xorshift() {
    static u_int y = 2463534242u;
    y ^= y << 13;
    y ^= y >> 17;
    y ^= y << 5;
    return y;
}
int main() {
    int me = env->env_id & 0xff;
    for (int i = 0; i < me; i++) {
        xorshift();
    }
    debugf("[xorshift %d] start\n", me);
    for (int i = 0; i < (1 << 24); i++) {
        v[xorshift() & ((1 << N) - 1)] += i & 0xffff;
    }
    int hash = 0;
    for (int i = 0; i < (1 << N); i++) {
        hash ^= v[i];
    }
    debugf("[xorshift %d] hash: %d\n", me, hash);
    return 0;
}
