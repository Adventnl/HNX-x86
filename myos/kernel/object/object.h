/* Kernel object model: refcounted, typed, id-tagged objects plus a global
 * registry and a per-table handle space.
 *
 * A kobject embeds a refcount, a type tag, a unique id and a release callback.
 * The registry maps id -> kobject for global lookup/iteration (diagnostics,
 * debugging). A handle_table maps small integer handles -> kobject for exposing
 * objects to a constrained context (a process, a driver) without leaking
 * pointers; closing a handle drops the reference.
 */
#ifndef MYOS_OBJECT_OBJECT_H
#define MYOS_OBJECT_OBJECT_H

#include "types.h"
#include "refcount.h"
#include "list.h"

enum kobject_type {
    KOBJ_NONE = 0,
    KOBJ_GENERIC,
    KOBJ_DEVICE,
    KOBJ_FILE,
    KOBJ_PROCESS,
    KOBJ_THREAD,
    KOBJ_SOCKET,
    KOBJ_PIPE,
    KOBJ_TIMER,
    KOBJ_MAX
};

struct kobject {
    struct refcount    ref;
    enum kobject_type  type;
    uint64_t           id;
    const char        *name;
    void             (*release)(struct kobject *);
    struct list_node   registry_link;
    void              *priv;        /* owner payload */
};

void kobject_subsystem_init(void);

void kobject_init(struct kobject *o, enum kobject_type type, const char *name,
                  void (*release)(struct kobject *));
void kobject_get(struct kobject *o);
/* Drops a reference; invokes release() and unregisters when it hits zero. */
void kobject_put(struct kobject *o);
int32_t kobject_refs(const struct kobject *o);

/* Registry. */
void           kobject_register(struct kobject *o);   /* assigns id, links */
void           kobject_unregister(struct kobject *o);
struct kobject *kobject_lookup(uint64_t id);
size_t         kobject_count(void);
size_t         kobject_count_by_type(enum kobject_type type);
const char    *kobject_type_name(enum kobject_type type);

/* ---- Handle table --------------------------------------------------------- */

struct handle_slot {
    struct kobject *obj;
    uint32_t        rights;   /* opaque permission bits */
    int             used;
};

struct handle_table {
    struct handle_slot *slots;
    uint32_t            capacity;
    uint32_t            count;
    uint32_t            next_hint;
};

int  handle_table_init(struct handle_table *t, uint32_t capacity);
void handle_table_destroy(struct handle_table *t);   /* drops all references */
/* Install obj (takes a reference). Returns a handle >= 0 or -1 when full. */
int  handle_install(struct handle_table *t, struct kobject *obj, uint32_t rights);
struct kobject *handle_lookup(struct handle_table *t, int handle, uint32_t *rights);
/* Close a handle; drops the reference. Returns 0 on success, -1 if invalid. */
int  handle_close(struct handle_table *t, int handle);
uint32_t handle_count(const struct handle_table *t);

#endif /* MYOS_OBJECT_OBJECT_H */
