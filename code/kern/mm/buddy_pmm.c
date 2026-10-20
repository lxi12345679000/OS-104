#include <buddy_pmm.h>
#include <list.h>
#include <pmm.h>
#include <stdio.h>

/*
 * A compact buddy allocator for the lab2 PMM framework.
 *
 * Block size at order k is 2^k pages.  Every free block is represented by
 * its first Page descriptor; only that head page has PG_property set.
 * Page.property stores the block size in pages.
 */

#define MAX_ORDER 16

static free_area_t buddy_free_area[MAX_ORDER + 1];
static size_t buddy_nr_free;

static inline size_t
buddy_block_size(int order) {
    return (size_t)1 << order;
}

static inline int
buddy_order_of(size_t npages) {
    int order = 0;
    size_t size = 1;
    while (size < npages) {
        size <<= 1;
        order++;
    }
    return order;
}

static inline size_t
buddy_page_index(struct Page *page) {
    return (size_t)(page - pages);
}

static void
buddy_list_insert(int order, struct Page *base) {
    list_entry_t *head = &buddy_free_area[order].free_list;
    list_entry_t *le = head;

    while ((le = list_next(le)) != head) {
        struct Page *page = le2page(le, page_link);
        if (base < page) {
            list_add_before(le, &base->page_link);
            return;
        }
    }
    list_add_before(head, &base->page_link);
}

/* Add a block to an order list without changing the global free-page count. */
static void
buddy_add_free_block(struct Page *base, int order) {
    size_t size = buddy_block_size(order);
    assert(order >= 0 && order <= MAX_ORDER);
    assert(PageProperty(base) == 0);
    base->property = size;
    SetPageProperty(base);
    buddy_list_insert(order, base);
    buddy_free_area[order].nr_free += size;
}

static void
buddy_remove_free_block(struct Page *base, int order) {
    size_t size = buddy_block_size(order);
    assert(order >= 0 && order <= MAX_ORDER);
    assert(PageProperty(base) != 0);
    assert(base->property == size);

    list_del(&base->page_link);
    buddy_free_area[order].nr_free -= size;
    ClearPageProperty(base);
    base->property = 0;
}

static void
buddy_init(void) {
    for (int order = 0; order <= MAX_ORDER; order++) {
        list_init(&buddy_free_area[order].free_list);
        buddy_free_area[order].nr_free = 0;
    }
    buddy_nr_free = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    size_t offset = 0;
    while (offset < n) {
        size_t remain = n - offset;
        size_t index = buddy_page_index(base) + offset;
        int order = 0;

        /*
         * Pick the largest power-of-two block that is aligned in the Page
         * descriptor array.  The first block may therefore be smaller than
         * the following blocks.
         */
        while (order < MAX_ORDER) {
            size_t next_size = buddy_block_size(order + 1);
            if (next_size > remain || (index & (next_size - 1)) != 0) {
                break;
            }
            order++;
        }

        size_t size = buddy_block_size(order);
        struct Page *block = base + offset;
        for (size_t i = 0; i < size; i++) {
            struct Page *page = block + i;
            assert(PageReserved(page));
            page->flags = 0;
            page->property = 0;
            set_page_ref(page, 0);
        }

        buddy_add_free_block(block, order);
        buddy_nr_free += size;
        offset += size;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);

    int need_order = buddy_order_of(n);
    if (need_order > MAX_ORDER) {
        return NULL;
    }

    int order = need_order;
    while (order <= MAX_ORDER &&
           list_empty(&buddy_free_area[order].free_list)) {
        order++;
    }
    if (order > MAX_ORDER) {
        return NULL;
    }

    list_entry_t *head = &buddy_free_area[order].free_list;
    struct Page *block = le2page(list_next(head), page_link);

    buddy_remove_free_block(block, order);

    /* Split larger blocks until the requested order is reached. */
    for (int k = order - 1; k >= need_order; k--) {
        struct Page *buddy = block + buddy_block_size(k);
        buddy->flags = 0;
        buddy->property = 0;
        buddy_add_free_block(buddy, k);
    }

    size_t alloc_size = buddy_block_size(need_order);
    buddy_nr_free -= alloc_size;

    /*
     * The allocated block keeps its real size in property while PG_property
     * is clear.  This lets free_pages() recover the buddy block size when a
     * caller requests a non-power-of-two allocation.
     */
    for (size_t i = 0; i < alloc_size; i++) {
        struct Page *page = block + i;
        page->flags = 0;
        page->property = 0;
        set_page_ref(page, 0);
    }
    block->property = alloc_size;
    ClearPageProperty(block);

    return block;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);

    size_t size = base->property;
    if (size == 0 || (size & (size - 1)) != 0) {
        size = buddy_block_size(buddy_order_of(n));
    }
    assert(n <= size);
    int order = buddy_order_of(size);
    assert(order <= MAX_ORDER);

    for (size_t i = 0; i < size; i++) {
        struct Page *page = base + i;
        assert(!PageReserved(page));
        assert(!PageProperty(page));
        page->flags = 0;
        page->property = 0;
        set_page_ref(page, 0);
    }

    base->property = size;
    SetPageProperty(base);
    buddy_free_area[order].nr_free += size;
    buddy_nr_free += size;
    buddy_list_insert(order, base);

    /* Merge with the buddy repeatedly while both blocks are free. */
    while (order < MAX_ORDER) {
        size_t index = buddy_page_index(base);
        size_t buddy_index = index ^ size;
        size_t page_count = npage - nbase;

        if (buddy_index >= page_count) {
            break;
        }

        struct Page *buddy = pages + buddy_index;
        if (!PageProperty(buddy) || buddy->property != size) {
            break;
        }

        buddy_remove_free_block(buddy, order);
        buddy_remove_free_block(base, order);

        struct Page *merged = (base < buddy) ? base : buddy;
        size <<= 1;
        order++;

        merged->flags = 0;
        merged->property = size;
        SetPageProperty(merged);
        buddy_free_area[order].nr_free += size;
        buddy_list_insert(order, merged);
        base = merged;
    }
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

static size_t
buddy_count_free_pages(void) {
    size_t total = 0;
    for (int order = 0; order <= MAX_ORDER; order++) {
        list_entry_t *head = &buddy_free_area[order].free_list;
        list_entry_t *le = head;
        size_t order_total = 0;
        while ((le = list_next(le)) != head) {
            struct Page *page = le2page(le, page_link);
            size_t size = buddy_block_size(order);
            assert(PageProperty(page));
            assert(page->property == size);
            assert(!PageReserved(page));
            size_t index = buddy_page_index(page);
            assert((index & (size - 1)) == 0);
            assert(index + size <= npage - nbase);
            total += page->property;
            order_total += page->property;
        }
        assert(order_total == buddy_free_area[order].nr_free);
    }
    return total;
}

static void
buddy_check(void) {
    size_t initial = buddy_nr_free;
    assert(buddy_count_free_pages() == initial);

    struct Page *p0 = alloc_pages(1);
    struct Page *p1 = alloc_pages(2);
    struct Page *p2 = alloc_pages(3);
    struct Page *p3 = alloc_pages(5);
    assert(p0 != NULL && p1 != NULL && p2 != NULL && p3 != NULL);
    assert(p0 != p1 && p0 != p2 && p0 != p3);
    assert(p1 != p2 && p1 != p3 && p2 != p3);
    assert(p0 + p0->property <= p1 || p1 + p1->property <= p0);
    assert(p0 + p0->property <= p2 || p2 + p2->property <= p0);
    assert(p0 + p0->property <= p3 || p3 + p3->property <= p0);
    assert(p1 + p1->property <= p2 || p2 + p2->property <= p1);
    assert(p1 + p1->property <= p3 || p3 + p3->property <= p1);
    assert(p2 + p2->property <= p3 || p3 + p3->property <= p2);

    size_t allocated = p0->property + p1->property + p2->property + p3->property;
    assert(buddy_nr_free + allocated == initial);

    free_pages(p0, 1);
    free_pages(p1, 2);
    free_pages(p2, 3);
    free_pages(p3, 5);
    assert(buddy_nr_free == initial);
    assert(buddy_count_free_pages() == initial);

    /* Requests larger than the maximum order must fail cleanly. */
    assert(alloc_pages((size_t)1 << (MAX_ORDER + 1)) == NULL);

    /* Exercise repeated split/merge at a single order. */
    struct Page *a = alloc_pages(8);
    struct Page *b = alloc_pages(8);
    assert(a != NULL && b != NULL);
    free_pages(a, 8);
    free_pages(b, 8);
    assert(buddy_nr_free == initial);
    assert(buddy_count_free_pages() == initial);

    cprintf("Buddy: PASS\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
