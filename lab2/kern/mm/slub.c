#include <pmm.h>
#include <list.h>
#include <string.h>
#include <slub.h>
#include <defs.h>
#include <stdio.h>
#define MAX_SIZE_CLASSES 9
static const size_t size_classes[MAX_SIZE_CLASSES] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048
};

struct slub_obj_head {
    size_t size_class_index;
};

struct kmem_cache {
    size_t size;
    list_entry_t free_list;
    size_t nr_free;
};

static struct kmem_cache caches[MAX_SIZE_CLASSES];

void slub_init(void) {
    for (int i = 0; i < MAX_SIZE_CLASSES; i++) {
        caches[i].size = size_classes[i];
        list_init(&caches[i].free_list);
        caches[i].nr_free = 0;
    }
}

static void slub_grow(struct kmem_cache *cache) {
    struct Page *page = alloc_pages(1);
    if (page == NULL) return;

uintptr_t page_va = page2pa(page) + va_pa_offset;8
    size_t obj_size = sizeof(struct slub_obj_head) + cache->size;
    size_t nr_objs = PGSIZE / obj_size;
    
    for (size_t i = 0; i < nr_objs; i++) {
        uintptr_t obj_va = page_va + i * obj_size;
        struct slub_obj_head *head = (struct slub_obj_head *)obj_va;
        head->size_class_index = cache->size;
        
        list_entry_t *obj_node = (list_entry_t *)(obj_va + sizeof(struct slub_obj_head));
        list_add(&cache->free_list, obj_node);
        cache->nr_free++;
    }
}

void *slub_alloc(size_t size) {
    if (size == 0) return NULL;
    
    int class_idx = -1;
    for (int i = 0; i < MAX_SIZE_CLASSES; i++) {
        if (size_classes[i] >= size) {
            class_idx = i;
            break;
        }
    }
    if (class_idx == -1) return NULL;

    struct kmem_cache *cache = &caches[class_idx];
    if (list_empty(&cache->free_list)) {
        slub_grow(cache);
        if (list_empty(&cache->free_list)) return NULL;
    }

    list_entry_t *le = list_next(&cache->free_list);
    list_del(le);
    cache->nr_free--;
    
    return (void *)le;
}

void slub_free(void *ptr) {
    if (ptr == NULL) return;
    
    struct slub_obj_head *head = (struct slub_obj_head *)((uintptr_t)ptr - sizeof(struct slub_obj_head));
    
    int class_idx = -1;
    for (int i = 0; i < MAX_SIZE_CLASSES; i++) {
        if (size_classes[i] == head->size_class_index) {
            class_idx = i;
            break;
        }
    }
    if (class_idx == -1) return;

    struct kmem_cache *cache = &caches[class_idx];
    list_entry_t *obj_node = (list_entry_t *)ptr;
    list_add(&cache->free_list, obj_node);
    cache->nr_free++;
}

void slub_check(void) {
    slub_init();
    
    assert(slub_alloc(0) == NULL);

    void *p1 = slub_alloc(16);
    void *p2 = slub_alloc(32);
    assert(p1 != NULL && p2 != NULL);
    assert(p1 != p2);

    slub_free(p1);
    void *p3 = slub_alloc(16);
    assert(p1 == p3);

    slub_free(p2);
    slub_free(p3);
    
    assert(slub_alloc(4096) == NULL);

    cprintf("slub_check() succeeded!\n");
}