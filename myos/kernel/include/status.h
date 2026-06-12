/* Kernel status / result codes. */
#ifndef MYOS_KERNEL_STATUS_H
#define MYOS_KERNEL_STATUS_H

typedef int kstatus_t;

#define K_OK            0    /* success                         */
#define K_ERR          -1    /* generic failure                 */
#define K_ERR_NOMEM    -2    /* out of memory                   */
#define K_ERR_INVALID  -3    /* invalid argument                */
#define K_ERR_NOTFOUND -4    /* not mapped / not present        */
#define K_ERR_EXISTS   -5    /* already present                 */
#define K_ERR_ALIGN    -6    /* alignment requirement violated  */

#define K_SUCCESS(s) ((s) == K_OK)
#define K_FAILED(s)  ((s) != K_OK)

#endif /* MYOS_KERNEL_STATUS_H */
