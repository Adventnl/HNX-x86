/* serviced: a minimal userland service manager foundation.
 *
 * Holds a static registry of system services (the init service registry, the
 * devd hardware-event daemon, logd, netd and mountd foundations), "starts" each
 * by running its check routine, and reports status. This is the scaffolding the
 * real long-running daemons will plug into; for now each service's start is a
 * foundation that validates its prerequisites via existing syscalls.
 */
#include "stdio.h"
#include "unistd.h"

enum svc_state { SVC_STOPPED = 0, SVC_RUNNING, SVC_FAILED };

struct service {
    const char *name;
    const char *desc;
    int       (*start)(void);
    enum svc_state state;
};

/* ---- service start routines (foundations) -------------------------------- */

static int svc_init(void) {
    /* The init service is us-adjacent; success if we have a pid. */
    return getpid() > 0 ? 0 : -1;
}

static int svc_devd(void) {
    /* devd watches hardware: confirm the kernel exposes a device table. */
    struct sys_device_entry d[8];
    int n = devices(d, 8);
    return n >= 0 ? 0 : -1;
}

static int svc_logd(void) {
    /* logd would drain the kernel log ring; foundation: confirm we can write. */
    return write(1, "", 0) == 0 ? 0 : -1;
}

static int svc_netd(void) {
    /* netd manages interfaces; foundation: query hardware info (NIC count). */
    struct sys_hw_info hw;
    return hw_info(&hw) == 0 ? 0 : -1;
}

static int svc_mountd(void) {
    /* mountd tracks mounts: confirm the mount table is reachable. */
    struct sys_mount_entry m[8];
    int n = mounts(m, 8);
    return n >= 0 ? 0 : -1;
}

static struct service g_services[] = {
    { "init",   "process-1 service registry",   svc_init,   SVC_STOPPED },
    { "devd",   "hardware event daemon",        svc_devd,   SVC_STOPPED },
    { "logd",   "kernel log daemon",            svc_logd,   SVC_STOPPED },
    { "netd",   "network daemon",               svc_netd,   SVC_STOPPED },
    { "mountd", "mount table daemon",           svc_mountd, SVC_STOPPED },
};
#define NSVC (int)(sizeof(g_services) / sizeof(g_services[0]))

static const char *state_str(enum svc_state s) {
    switch (s) {
    case SVC_RUNNING: return "running";
    case SVC_FAILED:  return "failed";
    default:          return "stopped";
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int started = 0;
    print("serviced: starting system services\n");
    for (int i = 0; i < NSVC; i++) {
        int rc = g_services[i].start();
        g_services[i].state = (rc == 0) ? SVC_RUNNING : SVC_FAILED;
        if (rc == 0) {
            started++;
        }
        printf("  %-8s %-28s [%s]\n",
               g_services[i].name, g_services[i].desc,
               state_str(g_services[i].state));
    }
    printf("serviced: %d/%d services running\n", started, NSVC);
    if (started == NSVC) {
        print("[PASS] service manager foundation\n");
        return 0;
    }
    print("[FAIL] service manager foundation\n");
    return 1;
}
