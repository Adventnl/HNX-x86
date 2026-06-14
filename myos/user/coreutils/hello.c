/* hello: a tiny program spawned by spawn_test; greets and exits 0. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    printf("hello, world from hello.hxe (pid %ld)\n", getpid());
    return 0;
}
