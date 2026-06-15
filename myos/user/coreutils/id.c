/* id: print the process credentials and identity. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    long uid = getuid();
    long gid = getgid();
    long pid = getpid();
    long ppid = getppid();
    long pgid = getpgid(0);
    long sid = getsid(0);
    printf("uid=%ld gid=%ld pid=%ld ppid=%ld pgid=%ld sid=%ld\n",
           uid, gid, pid, ppid, pgid, sid);
    return 0;
}
