#include <vmm.h>
#include <sync.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <error.h>
#include <pmm.h>
#include <riscv.h>
#include <kmalloc.h>

/*
  vmm design include two parts: mm_struct (mm) & vma_struct (vma)
  mm is the memory manager for the set of continuous virtual memory
  area which have the same PDT. vma is a continuous virtual memory area.
  There a linear link list for vma & a redblack link list for vma in mm.
---------------
  mm related functions:
   golbal functions
     struct mm_struct * mm_create(void)
     void mm_destroy(struct mm_struct *mm)
     int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr)
--------------
  vma related functions:
   global functions
     struct vma_struct * vma_create (uintptr_t vm_start, uintptr_t vm_end,...)
     void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
     struct vma_struct * find_vma(struct mm_struct *mm, uintptr_t addr)
   local functions
     inline void check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
---------------
   check correctness functions
     void check_vmm(void);
     void check_vma_struct(void);
     void check_pgfault(void);
*/

static void check_vmm(void);
static void check_vma_struct(void);

// mm_create -  alloc a mm_struct & initialize it.
struct mm_struct *
mm_create(void)
{
    struct mm_struct *mm = kmalloc(sizeof(struct mm_struct));

    if (mm != NULL)
    {
        list_init(&(mm->mmap_list));
        mm->mmap_cache = NULL;
        mm->pgdir = NULL;
        mm->map_count = 0;

        mm->sm_priv = NULL;

        set_mm_count(mm, 0);
        lock_init(&(mm->mm_lock));
    }
    return mm;
}

// vma_create - alloc a vma_struct & initialize it. (addr range: vm_start~vm_end)
struct vma_struct *
vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags)
{
    struct vma_struct *vma = kmalloc(sizeof(struct vma_struct));

    if (vma != NULL)
    {
        vma->vm_start = vm_start;
        vma->vm_end = vm_end;
        vma->vm_flags = vm_flags;
    }
    return vma;
}

// find_vma - find a vma  (vma->vm_start <= addr <= vma_vm_end)
struct vma_struct *
find_vma(struct mm_struct *mm, uintptr_t addr)
{
    struct vma_struct *vma = NULL;
    if (mm != NULL)
    {
        vma = mm->mmap_cache;
        if (!(vma != NULL && vma->vm_start <= addr && vma->vm_end > addr))
        {
            bool found = 0;
            list_entry_t *list = &(mm->mmap_list), *le = list;
            while ((le = list_next(le)) != list)
            {
                vma = le2vma(le, list_link);
                if (vma->vm_start <= addr && addr < vma->vm_end)
                {
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                vma = NULL;
            }
        }
        if (vma != NULL)
        {
            mm->mmap_cache = vma;
        }
    }
    return vma;
}

// check_vma_overlap - check if vma1 overlaps vma2 ?
static inline void
check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
{
    assert(prev->vm_start < prev->vm_end);
    assert(prev->vm_end <= next->vm_start);
    assert(next->vm_start < next->vm_end);
}

// insert_vma_struct -insert vma in mm's list link
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
{
    assert(vma->vm_start < vma->vm_end);
    list_entry_t *list = &(mm->mmap_list);
    list_entry_t *le_prev = list, *le_next;

    list_entry_t *le = list;
    while ((le = list_next(le)) != list)
    {
        struct vma_struct *mmap_prev = le2vma(le, list_link);
        if (mmap_prev->vm_start > vma->vm_start)
        {
            break;
        }
        le_prev = le;
    }

    le_next = list_next(le_prev);

    /* check overlap */
    if (le_prev != list)
    {
        check_vma_overlap(le2vma(le_prev, list_link), vma);
    }
    if (le_next != list)
    {
        check_vma_overlap(vma, le2vma(le_next, list_link));
    }

    vma->vm_mm = mm;
    list_add_after(le_prev, &(vma->list_link));

    mm->map_count++;
}

// mm_destroy - free mm and mm internal fields
void mm_destroy(struct mm_struct *mm)
{
    assert(mm_count(mm) == 0);

    list_entry_t *list = &(mm->mmap_list), *le;
    while ((le = list_next(list)) != list)
    {
        list_del(le);
        kfree(le2vma(le, list_link)); // kfree vma
    }
    kfree(mm); // kfree mm
    mm = NULL;
}

int mm_map(struct mm_struct *mm, uintptr_t addr, size_t len, uint32_t vm_flags,
           struct vma_struct **vma_store)
{
    uintptr_t start = ROUNDDOWN(addr, PGSIZE), end = ROUNDUP(addr + len, PGSIZE);
    if (!USER_ACCESS(start, end))
    {
        return -E_INVAL;
    }

    assert(mm != NULL);

    int ret = -E_INVAL;

    struct vma_struct *vma;
    if ((vma = find_vma(mm, start)) != NULL && end > vma->vm_start)
    {
        goto out;
    }
    ret = -E_NO_MEM;

    if ((vma = vma_create(start, end, vm_flags)) == NULL)
    {
        goto out;
    }
    insert_vma_struct(mm, vma);
    if (vma_store != NULL)
    {
        *vma_store = vma;
    }
    ret = 0;

out:
    return ret;
}

//lab5 function3 2412105
// dup_mmap - 复制 from 地址空间中的全部 VMA 到 to
//
// 2412105：lab5 的 fork 采用写时复制，因此普通可写页面通过
// copy_range(..., share=true) 共享，并在两侧 PTE 上打 PTE_COW 标记。
// 失败时由调用方 copy_mm 统一回收已经插入的 VMA 和页面，保证不会留下
// 半完成的地址空间。
int
dup_mmap(struct mm_struct *to, struct mm_struct *from)
{
    assert(to != NULL && from != NULL);
    assert(to->pgdir != NULL && from->pgdir != NULL);

    list_entry_t *list = &(from->mmap_list);
    list_entry_t *le = list;

    while ((le = list_next(le)) != list)
    {
        struct vma_struct *vma = le2vma(le, list_link);

        // 保持 VMA 的区间和权限（含 VM_STACK），插入顺序与原链表一致。
        struct vma_struct *new_vma =
            vma_create(vma->vm_start, vma->vm_end, vma->vm_flags);

        if (new_vma == NULL)
        {
            return -E_NO_MEM;
        }

        insert_vma_struct(to, new_vma);

        // share == true：共享物理页，可写页转为只读 + PTE_COW。
        if (copy_range(to->pgdir, from->pgdir,
                       vma->vm_start, vma->vm_end, 1) != 0)
        {
            return -E_NO_MEM;
        }
    }

    return 0;
}

//lab5 function3 2412105
// do_pgfault - 处理普通按需分页与 COW 写缺页
//
// 2412105：调用方（trap.c）已经把异常原因翻译为软件错误码：
//   PFAULT_WRITE  本次访问是写
//   PFAULT_PRESENT 触发异常的 PTE 已经有效（COW 写保护就是这种情况）
//
// 返回 0 表示缺页已处理，可以返回用户态继续执行；负值表示失败。
int
do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr)
{
    if (mm == NULL || mm->pgdir == NULL)
    {
        return -E_INVAL;
    }

    // 1. 地址必须落在当前地址空间的某个 VMA 内。
    struct vma_struct *vma = find_vma(mm, addr);

    if (vma == NULL || addr < vma->vm_start || addr >= vma->vm_end)
    {
        return -E_INVAL;
    }

    bool write = (error_code & PFAULT_WRITE) != 0;

    // 2. VMA 权限检查：写缺页必须有 VM_WRITE，读缺页必须有 VM_READ。
    if (write && !(vma->vm_flags & VM_WRITE))
    {
        return -E_INVAL;
    }

    if (!write && !(vma->vm_flags & VM_READ))
    {
        return -E_INVAL;
    }

    uintptr_t la = ROUNDDOWN(addr, PGSIZE);
    pte_t *ptep = get_pte(mm->pgdir, la, 1);

    if (ptep == NULL)
    {
        return -E_NO_MEM;
    }

    // 3. PTE 已经有效：只可能是 COW 写保护缺页。
    if (*ptep & PTE_V)
    {
        if (!write || !(*ptep & PTE_COW))
        {
            // 非 COW 的有效 PTE 再次缺页，说明是真正的权限错误。
            return -E_INVAL;
        }

        struct Page *page = pte2page(*ptep);

        if (page_ref(page) > 1)
        {
            // 3.1 仍有其他进程共享该物理页：复制一份私有页。
            struct Page *new_page = alloc_page();

            if (new_page == NULL)
            {
                return -E_NO_MEM;
            }

            memcpy(KADDR(page2pa(new_page)),
                   KADDR(page2pa(page)),
                   PGSIZE);

            // 新页恢复 VMA 允许的写权限，并去掉 COW 标记。
            uint32_t perm = (*ptep & 0x3FF);
            perm = (perm | PTE_W) & ~PTE_COW;

            if (page_insert(mm->pgdir, new_page, la, perm) != 0)
            {
                // page_insert 失败时旧映射保持不变，释放刚分配的页面。
                free_page(new_page);
                return -E_NO_MEM;
            }
        }
        else
        {
            // 3.2 当前进程是最后一个使用者：直接恢复写权限即可。
            *ptep = (*ptep | PTE_W) & ~PTE_COW;
            tlb_invalidate(mm->pgdir, la);
        }

        return 0;
    }

    // 4. PTE 尚未建立：按 VMA 权限做按需分配。
    uint32_t perm = PTE_U;

    if (vma->vm_flags & VM_READ)
    {
        perm |= PTE_R;
    }

    if (vma->vm_flags & VM_WRITE)
    {
        perm |= PTE_R | PTE_W;
    }

    if (vma->vm_flags & VM_EXEC)
    {
        perm |= PTE_X;
    }

    struct Page *page = alloc_page();

    if (page == NULL)
    {
        return -E_NO_MEM;
    }

    memset(KADDR(page2pa(page)), 0, PGSIZE);

    if (page_insert(mm->pgdir, page, la, perm) != 0)
    {
        free_page(page);
        return -E_NO_MEM;
    }

    return 0;
}


void exit_mmap(struct mm_struct *mm)
{
    assert(mm != NULL && mm_count(mm) == 0);
    pde_t *pgdir = mm->pgdir;
    list_entry_t *list = &(mm->mmap_list), *le = list;
    while ((le = list_next(le)) != list)
    {
        struct vma_struct *vma = le2vma(le, list_link);
        unmap_range(pgdir, vma->vm_start, vma->vm_end);
    }
    while ((le = list_next(le)) != list)
    {
        struct vma_struct *vma = le2vma(le, list_link);
        exit_range(pgdir, vma->vm_start, vma->vm_end);
    }
}

bool copy_from_user(struct mm_struct *mm, void *dst, const void *src, size_t len, bool writable)
{
    if (!user_mem_check(mm, (uintptr_t)src, len, writable))
    {
        return 0;
    }
    memcpy(dst, src, len);
    return 1;
}

bool copy_to_user(struct mm_struct *mm, void *dst, const void *src, size_t len)
{
    if (!user_mem_check(mm, (uintptr_t)dst, len, 1))
    {
        return 0;
    }
    memcpy(dst, src, len);
    return 1;
}

// vmm_init - initialize virtual memory management
//          - now just call check_vmm to check correctness of vmm
void vmm_init(void)
{
    check_vmm();
}

// check_vmm - check correctness of vmm
static void
check_vmm(void)
{
    // size_t nr_free_pages_store = nr_free_pages();

    check_vma_struct();
    // check_pgfault();

    cprintf("check_vmm() succeeded.\n");
}

static void
check_vma_struct(void)
{
    // size_t nr_free_pages_store = nr_free_pages();

    struct mm_struct *mm = mm_create();
    assert(mm != NULL);

    int step1 = 10, step2 = step1 * 10;

    int i;
    for (i = step1; i >= 1; i--)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    for (i = step1 + 1; i <= step2; i++)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    list_entry_t *le = list_next(&(mm->mmap_list));

    for (i = 1; i <= step2; i++)
    {
        assert(le != &(mm->mmap_list));
        struct vma_struct *mmap = le2vma(le, list_link);
        assert(mmap->vm_start == i * 5 && mmap->vm_end == i * 5 + 2);
        le = list_next(le);
    }

    for (i = 5; i <= 5 * step2; i += 5)
    {
        struct vma_struct *vma1 = find_vma(mm, i);
        assert(vma1 != NULL);
        struct vma_struct *vma2 = find_vma(mm, i + 1);
        assert(vma2 != NULL);
        struct vma_struct *vma3 = find_vma(mm, i + 2);
        assert(vma3 == NULL);
        struct vma_struct *vma4 = find_vma(mm, i + 3);
        assert(vma4 == NULL);
        struct vma_struct *vma5 = find_vma(mm, i + 4);
        assert(vma5 == NULL);

        assert(vma1->vm_start == i && vma1->vm_end == i + 2);
        assert(vma2->vm_start == i && vma2->vm_end == i + 2);
    }

    for (i = 4; i >= 0; i--)
    {
        struct vma_struct *vma_below_5 = find_vma(mm, i);
        if (vma_below_5 != NULL)
        {
            cprintf("vma_below_5: i %x, start %x, end %x\n", i, vma_below_5->vm_start, vma_below_5->vm_end);
        }
        assert(vma_below_5 == NULL);
    }

    mm_destroy(mm);

    cprintf("check_vma_struct() succeeded!\n");
}
bool user_mem_check(struct mm_struct *mm, uintptr_t addr, size_t len, bool write)
{
    if (mm != NULL)
    {
        if (!USER_ACCESS(addr, addr + len))
        {
            return 0;
        }
        struct vma_struct *vma;
        uintptr_t start = addr, end = addr + len;
        while (start < end)
        {
            if ((vma = find_vma(mm, start)) == NULL || start < vma->vm_start)
            {
                return 0;
            }
            if (!(vma->vm_flags & ((write) ? VM_WRITE : VM_READ)))
            {
                return 0;
            }
            if (write && (vma->vm_flags & VM_STACK))
            {
                if (start < vma->vm_start + PGSIZE)
                { // check stack start & size
                    return 0;
                }
            }
            start = vma->vm_end;
        }
        return 1;
    }
    return KERN_ACCESS(addr, addr + len);
}