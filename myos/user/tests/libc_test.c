/* Work Unit G: libc runtime test. Exercises the freestanding user libc
 * (string, stdlib, ctype, stdio, malloc). Marker: "[PASS] libc runtime tests". */
#include "utest.h"
#include "string.h"
#include "stdlib.h"
#include "ctype.h"
#include "stdio.h"

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    return ia - ib;
}

int main(void) {
    UT_BEGIN();

    /* string */
    UT_CHECK_EQ(strlen("hello"), 5, "strlen");
    UT_CHECK(strcmp("abc", "abc") == 0, "strcmp eq");
    UT_CHECK(strcmp("abc", "abd") < 0, "strcmp lt");
    UT_CHECK(strncmp("abcXY", "abcZZ", 3) == 0, "strncmp prefix");
    char buf[32];
    strcpy(buf, "foo");
    strcat(buf, "bar");
    UT_CHECK(strcmp(buf, "foobar") == 0, "strcpy+strcat");
    UT_CHECK(strchr(buf, 'b') == buf + 3, "strchr");
    UT_CHECK(strrchr(buf, 'o') == buf + 2, "strrchr");
    UT_CHECK(strstr(buf, "oba") == buf + 2, "strstr");
    char m1[8], m2[8];
    memset(m1, 0xAB, 8);
    memcpy(m2, m1, 8);
    UT_CHECK(memcmp(m1, m2, 8) == 0, "memcpy+memcmp");
    memmove(m1 + 1, m1, 7);
    UT_CHECK((unsigned char)m1[7] == 0xAB, "memmove overlap");

    /* stdlib */
    UT_CHECK_EQ(atoi("123"), 123, "atoi");
    UT_CHECK_EQ(atoi("-42"), -42, "atoi neg");
    UT_CHECK_EQ(strtol("ff", 0, 16), 255, "strtol hex");
    UT_CHECK_EQ(abs(-7), 7, "abs");
    int arr[6] = { 5, 2, 8, 1, 9, 3 };
    qsort(arr, 6, sizeof(int), cmp_int);
    UT_CHECK(arr[0] == 1 && arr[5] == 9, "qsort");
    int key = 8;
    int *found = (int *)bsearch(&key, arr, 6, sizeof(int), cmp_int);
    UT_CHECK(found && *found == 8, "bsearch");

    /* ctype */
    UT_CHECK(isdigit('7') && !isdigit('x'), "isdigit");
    UT_CHECK(isalpha('A') && !isalpha('3'), "isalpha");
    UT_CHECK(toupper('a') == 'A' && tolower('Z') == 'z', "to upper/lower");
    UT_CHECK(isspace(' ') && isspace('\t'), "isspace");

    /* stdio snprintf */
    char s[32];
    int n = snprintf(s, sizeof(s), "%d-%s-%x", 42, "ok", 255);
    UT_CHECK(strcmp(s, "42-ok-ff") == 0, "snprintf");
    UT_CHECK(n == 8, "snprintf return");

    /* malloc / realloc / free */
    char *p = (char *)malloc(16);
    UT_CHECK(p != 0, "malloc");
    strcpy(p, "data");
    p = (char *)realloc(p, 64);
    UT_CHECK(p != 0 && strcmp(p, "data") == 0, "realloc preserves");
    free(p);
    int *z = (int *)calloc(8, sizeof(int));
    UT_CHECK(z && z[0] == 0 && z[7] == 0, "calloc zeroes");
    free(z);

    UT_RESULT("libc runtime tests");
    return UT_FAILED() ? 1 : 0;
}
