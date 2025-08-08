<h2 style="text-align:center;font-size:36px;">Lab Copy on-write</h2>

# 1. Implement copy-on write(hard)
## 1.1 实验目的

在`xv6`内核中实现`copy-on-write fork`

## 1.2 实验步骤

1. 在`kernel/riscv.h`中加入宏定义
```c
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // 1 -> user can access
#define PTE_C (1L << 8)
```

2. 定义引用计数的全局变量`ref`
```c
struct ref_stru {
  struct spinlock lock;
  int cnt[PHYSTOP / PGSIZE];  // 引用计数
} ref;
```

3. 在`kinit`中初始化`ref`的自旋锁
```c
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref");
  freerange(end, (void*)PHYSTOP);
}
```

4. 修改`kalloc`和`kfree`函数，在`kalloc`中初始化内存引用计数为1，在`kfree`函数中对内存引用计数减1，如果引用计数为0时才真正删除
```c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 只有当引用计数为0了才回收空间
  // 否则只是将引用计数减1
  acquire(&ref.lock);
  if(--ref.cnt[(uint64)pa / PGSIZE] == 0) {
    release(&ref.lock);

    r = (struct run*)pa;

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  } else {
    release(&ref.lock);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    acquire(&ref.lock);
    ref.cnt[(uint64)r / PGSIZE] = 1;  // 将引用计数初始化为1
    release(&ref.lock);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

5. 修改`freerange`
```c
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    // 在kfree中将会对cnt[]减1，这里要先设为1，否则就会减成负数
    ref.cnt[(uint64)p / PGSIZE] = 1;
    kfree(p);
  }
}
```

6. 修改`uvmcopy`，不为子进程分配内存，而是使父子进程共享内存
```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);

    // 仅对可写页面设置COW标记
    if(flags & PTE_W) {
      // 禁用写并设置COW Fork标记
      flags = (flags | PTE_F) & ~PTE_W;
      *pte = PA2PTE(pa) | flags;
    }

    if(mappages(new, i, PGSIZE, pa, flags) != 0) {
      uvmunmap(new, 0, i / PGSIZE, 1);
      return -1;
    }
    // 增加内存的引用计数
    kaddrefcnt((char*)pa);
  }
  return 0;
}
```

7. 修改`usertrap`，处理页面错误
```c
else if((which_dev = devintr()) != 0){
  // ok
} else if(r_scause() == 13 || r_scause() == 15) {
  uint64 fault_va = r_stval();  // 获取出错的虚拟地址
  if(fault_va >= p->sz
    || cowpage(p->pagetable, fault_va) != 0
    || cowalloc(p->pagetable, PGROUNDDOWN(fault_va)) == 0)
    p->killed = 1;
} else {
  ...
}
```

## 1.3 实验中遇到的问题和解决办法

1. 引用计数结构体无法识别:

使用外部声明`extern struct ref_stru ref;`

2. 起初将 `COW` 标志与 `PTE_W`一同保留，导致无法触发写时异常，`COW` 无法生效:

在设置 `COW` 页时，只保留 `PTE_R`，去掉 `PTE_W`

3. 某次调试时，意外地把内核内存也标记为 `COW`，导致无法进入 `shell`:

加入地址检查逻辑
```C
if ((uint64)pa < (uint64)end || pa >= PHYSTOP)
```

## 1.4 实验心得

理解了操作系统中虚拟内存管理和进程复制机制的底层实现，`Copy-on-Write` 技术通过延迟复制内存页，提高了 `fork()` 的性能，是现代操作系统中非常常见的优化策略。

调试过程让我对 C 语言中的内存模型和编译链接原理有了更深入体会。