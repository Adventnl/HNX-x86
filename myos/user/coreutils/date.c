/* date: print wall-clock time from gettimeofday as a broken-down UTC calendar
 * date, plus the monotonic uptime. With -u (or default) prints UTC. */
#include "stdio.h"
#include "unistd.h"

static const char *months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static int is_leap(long y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct sys_timeval tv;
    if (gettimeofday(&tv) < 0) {
        /* Fall back to uptime only. */
        printf("uptime %lums\n", uptime_ms());
        return 0;
    }
    unsigned long long secs = tv.tv_sec;
    unsigned long long days = secs / 86400ULL;
    unsigned int rem = (unsigned int)(secs % 86400ULL);
    unsigned int hh = rem / 3600;
    unsigned int mm = (rem % 3600) / 60;
    unsigned int ss = rem % 60;

    /* Convert days-since-epoch (1970-01-01) to Y/M/D. */
    long year = 1970;
    while (1) {
        long dy = is_leap(year) ? 366 : 365;
        if ((long long)days < dy) break;
        days -= dy;
        year++;
    }
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int mon = 0;
    for (; mon < 12; mon++) {
        int dm = mdays[mon];
        if (mon == 1 && is_leap(year)) dm = 29;
        if ((long long)days < dm) break;
        days -= dm;
    }
    unsigned int day = (unsigned int)days + 1;

    printf("%s %2u %02u:%02u:%02u UTC %ld\n",
           months[mon], day, hh, mm, ss, year);
    return 0;
}
