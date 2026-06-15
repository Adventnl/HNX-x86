/* Kernel object model implementation (see kernel/object/object.h). */
#include "object.h"
#include "slab.h"
#include "string.h"
#include "log.h"
#include "fmt.h"

static struct list_node g_registry;
static uint64_t         g_next_id = 1;
static size_t           g_count;
static int              g_inited;

void kobject_subsystem_init(void) {
    list_init(&g_registry);
    g_next_id = 1;
    g_count = 0;
    g_inited = 1;
}

const char *kobject_type_name(enum kobject_type type) {
    switch (type) {
    case KOBJ_NONE:    return "none";
    case KOBJ_GENERIC: return "generic";
    case KOBJ_DEVICE:  return "device";
    case KOBJ_FILE:    return "file";
    case KOBJ_PROCESS: return "process";
    case KOBJ_THREAD:  return "thread";
    case KOBJ_SOCKET:  return "socket";
    case KOBJ_PIPE:    return "pipe";
    case KOBJ_TIMER:   return "timer";
    default:           return "?";
    }
}

void kobject_init(struct kobject *o, enum kobject_type type, const char *name,
                  void (*release)(struct kobject *)) {
    refcount_init(&o->ref, 1);
    o->type = type;
    o->id = 0;
    o->name = name;
    o->release = release;
    list_init(&o->registry_link);
    o->priv = NULL;
}

void kobject_get(struct kobject *o) {
    refcount_get(&o->ref);
}

void kobject_put(struct kobject *o) {
    if (refcount_put(&o->ref)) {
        if (list_linked(&o->registry_link)) {
            kobject_unregister(o);
        }
        if (o->release) {
            o->release(o);
        }
    }
}

int32_t kobject_refs(const struct kobject *o) {
    return refcount_read(&o->ref);
}

void kobject_register(struct kobject *o) {
    if (!g_inited) {
        kobject_subsystem_init();
    }
    if (o->id == 0) {
        o->id = g_next_id++;
    }
    list_add_tail(&o->registry_link, &g_registry);
    g_count++;
}

void kobject_unregister(struct kobject *o) {
    if (list_linked(&o->registry_link)) {
        list_del_init(&o->registry_link);
        if (g_count) {
            g_count--;
        }
    }
}

struct kobject *kobject_lookup(uint64_t id) {
    struct kobject *o;
    list_for_each_entry(o, &g_registry, registry_link) {
        if (o->id == id) {
            return o;
        }
    }
    return NULL;
}

size_t kobject_count(void) {
    return g_count;
}

size_t kobject_count_by_type(enum kobject_type type) {
    size_t n = 0;
    struct kobject *o;
    list_for_each_entry(o, &g_registry, registry_link) {
        if (o->type == type) {
            n++;
        }
    }
    return n;
}

/* ---- Handle table --------------------------------------------------------- */

int handle_table_init(struct handle_table *t, uint32_t capacity) {
    t->slots = (struct handle_slot *)kmem_zalloc(capacity * sizeof(struct handle_slot));
    if (!t->slots) {
        return -1;
    }
    t->capacity = capacity;
    t->count = 0;
    t->next_hint = 0;
    return 0;
}

void handle_table_destroy(struct handle_table *t) {
    if (!t->slots) {
        return;
    }
    for (uint32_t i = 0; i < t->capacity; i++) {
        if (t->slots[i].used && t->slots[i].obj) {
            kobject_put(t->slots[i].obj);
        }
    }
    kmem_free(t->slots);
    t->slots = NULL;
    t->capacity = t->count = 0;
}

int handle_install(struct handle_table *t, struct kobject *obj, uint32_t rights) {
    for (uint32_t pass = 0; pass < 2; pass++) {
        uint32_t start = pass == 0 ? t->next_hint : 0;
        uint32_t end = pass == 0 ? t->capacity : t->next_hint;
        for (uint32_t i = start; i < end; i++) {
            if (!t->slots[i].used) {
                t->slots[i].used = 1;
                t->slots[i].obj = obj;
                t->slots[i].rights = rights;
                kobject_get(obj);
                t->count++;
                t->next_hint = (i + 1 < t->capacity) ? i + 1 : 0;
                return (int)i;
            }
        }
    }
    return -1;
}

struct kobject *handle_lookup(struct handle_table *t, int handle, uint32_t *rights) {
    if (handle < 0 || (uint32_t)handle >= t->capacity) {
        return NULL;
    }
    struct handle_slot *s = &t->slots[handle];
    if (!s->used) {
        return NULL;
    }
    if (rights) {
        *rights = s->rights;
    }
    return s->obj;
}

int handle_close(struct handle_table *t, int handle) {
    if (handle < 0 || (uint32_t)handle >= t->capacity) {
        return -1;
    }
    struct handle_slot *s = &t->slots[handle];
    if (!s->used) {
        return -1;
    }
    struct kobject *obj = s->obj;
    s->used = 0;
    s->obj = NULL;
    s->rights = 0;
    if (t->count) {
        t->count--;
    }
    if (obj) {
        kobject_put(obj);
    }
    return 0;
}

uint32_t handle_count(const struct handle_table *t) {
    return t->count;
}
