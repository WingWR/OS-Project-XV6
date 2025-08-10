# Lab Page Tables

## 1. Print a page table (easy)
### 1.1 实验目的

定义一个名为`vmprint()`的函数。它应当接收一个`pagetable_t`作为参数，并以下面描述的格式打印该页表
```bash
page table 0x0000000087f6e000
..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
.. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
.. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
.. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
.. .. ..510: pte 0x0000000021fdd807 pa 0x0000000087f76000
.. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
```

### 1.2 实验步骤

1. 在`exec.c`中的`return argc`之前插入`if(p->pid==1) vmprint(p->pagetable);`

2. 在`kernel/vm.c`定义`vmprint()`函数
```c
void
_vmprint(pagetable_t pagetable, int level){
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(pte & PTE_V){
      for (int j = 0; j < level; j++){
        if (j) printf(" ");
        printf("..");
      }
      uint64 child = PTE2PA(pte);
      printf("%d: pte %p pa %p\n", i, pte, child);
      if((pte & (PTE_R|PTE_W|PTE_X)) == 0){
        _vmprint((pagetable_t)child, level + 1);
      }
    }
  }
}

void
vmprint(pagetable_t pagetable){
  printf("page table %p\n", pagetable);
  _vmprint(pagetable, 1);
}
```

3. 在`kernel/defs.h`中声明`vmprint()`函数
```c
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);
void            vmprint(pagetable_t);
```

### 1.3 实验中遇到的问题和解决办法

1. 未判断递归条件导致无限递归:

判断是否为叶节点采用 (pte & (PTE_R|PTE_W|PTE_X)) == 0，确保只对非叶子页表进行递归打印。

2. 未声明函数导致无法运行:

在`kernel/defs.h`中声明`vmprint()`函数
```c
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);
void            vmprint(pagetable_t);
```

### 1.4 实验心得

通过递归遍历页表，能够直观地查看每一级页表及其物理地址映射，极大帮助了对虚拟内存管理的认识

## 2. A kernel page table per process (hard)
### 2.1 实验目的

修改内核来让每一个进程在内核中执行时使用它自己的内核页表的副本。修改`struct proc`来为每一个进程维护一个内核页表，修改调度程序使得切换进程时也切换内核页表

### 2.2 实验步骤

1. 给`kernel/proc.h`里面的`struct proc`加上内核页表的字段:
```c
pagetable_t kpagetable;      // 进程的内核页表
```

2. 在 `kernel/vm.c` 中添加新函数 `vminit()`:
```c
void 
vminit(pagetable_t pagetable)
{
  // uart registers
  vmmap(pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  // virtio mmio disk interface
  vmmap(pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  // // CLINT
  // vmmap(pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  // PLIC
  vmmap(pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
  // map kernel text executable and read-only.
  vmmap(pagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
  // map kernel data and the physical RAM we'll make use of.
  vmmap(pagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  vmmap(pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}
```

3. 将 `procinit()` 函数中分配内核栈的代码移到 `allocproc()` 中:
```c
static struct proc*
allocproc(void)
{
    ...
  /** 创建内核页表 */
  p->kpagetable = proc_kpagetable(p);
  if(p->kpagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  /** 理顺内核栈的映射关系 */
  char *pa = kalloc();
  if(pa == 0)
    panic("kalloc");
  uint64 va = KSTACK(0);
  vmmap(p->kpagetable, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  p->kstack = va;
    ...
}
```

4. 在 `scheduler()` 函数中添加切换页表寄存器 `satp` 的指令:
```c
 ...
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;

        vminithart(p->kpagetable);
        ···
      }
    }
···
``` 

5. 释放内核页表，但不要释放最外围的物理内存页，封装为`proc_freekpagetable()`函数:
```c
void 
proc_freekpagetable(pagetable_t kpagetable, uint64 sz)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i=0; i<512; i++) {
    pte_t pte = kpagetable[i];
    if((pte & PTE_V) == 0) 
      continue;

    /** 该PTE有效（内部节点和叶子节点） */
    if((pte & (PTE_R|PTE_W|PTE_X)) == 0) {
      uint64 child = PTE2PA(pte);
      proc_freekpagetable((pagetable_t)child, sz);
      kpagetable[i] = 0;
    }
  }
  kfree((void*)kpagetable);
}
```

6. 在 `freeproc()` 中添加释放内核栈的功能代码:
```c
static void
freeproc(struct proc *p)
{
    ...
  p->xstate = 0;

  /** 在释放内核页表之前需要释放内核栈 */
  if(p->kstack)
    uvmunmap(p->kpagetable, p->kstack, 1, 1);
  p->kstack = 0;

  if(p->kpagetable)
    proc_freekpagetable(p->kpagetable, p->sz);
  p->kpagetable = 0;

  ...
  p->state = UNUSED;
}
```

7. 修改 `virtio_disk.c` 中的地址写入指令:
```c
disk.desc[idx[0]].addr = (uint64) vmpa(myproc()->kpagetable, (uint64) &buf0);
```

### 2.3 实验中遇到的问题和解决办法

1. 访问 `myproc()->kpagetable` 编译时报错，提示`struct proc`不完整或字段不存在:

确认所有相关文件包含了 `proc.h`

2. 重复定义 `struct spinlock`，引起编译错误:

给头文件加上防止重复包含的宏定义`(##ifndef/##define/##endif)`

3. 调用 `proc_kpagetable` 和 `proc_freekpagetable` 函数时提示隐式声明和链接错误:

在对应的头文件中声明函数原型

### 2.4 实验心得

本实验通过为每个进程维护独立的内核页表，增强了对多进程虚拟内存管理的理解。内核页表的独立复制有效避免了多个进程共享单一内核页表时可能产生的安全和隔离问题。实现过程中深入体会了页表结构的层次性以及内存映射的细节。

## 3. Simplify copyin/copyinstr (hard)
### 3.1 实验目的

将定义在`kernel/vm.c中的copyin`的主题内容替换为对`copyin_new`的调用；对`copyinstr`和`copyinstr_new`执行相同的操作

### 3.2 实验步骤

1. 首先添加`u2kvmcopy()`函数:
```c
int
u2kvmcopy(pagetable_t old, pagetable_t new, uint64 start, uint64 end)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i=PGROUNDUP(start); i<end; i+=PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("u2kvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("u2kvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte) & (~PTE_U);

    if(mappages(new, i, PGSIZE, pa, flags) != 0)
      goto err;
  }
  return 0;

 err:
  uvmunmap(new, start, (i-start)/PGSIZE, 0);
  return -1;
}
```

2. 更改`fork()`函数:
```c
···
if(u2kvmcopy(np->pagetable, np->kpagetable, 0, np->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
···
```

3. 修改`exec()`函数:
```c
···
uvmunmap(p->kpagetable, 0, PGROUNDUP(oldsz)/PGSIZE, 0);
/** 在替换原用户页表之后，将新用户页表塞进内核页表中 */
if(u2kvmcopy(p->pagetable, p->kpagetable, 0, p->sz) < 0) 
  goto bad;
···
```

4. 修改 `growproc()`函数:
```c
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(PGROUNDUP(sz+n) >= PLIC)
      return -1;
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) 
      return -1;
    if(u2kvmcopy(p->pagetable, p->kpagetable, p->sz, sz) < 0)
      return -1;
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);  
    sz = vmdealloc(p->kpagetable, p->sz, sz, 0);
  }
  p->sz = sz;
  return 0;
}
```

5. 修改`userinit()`函数:
```c
···
u2kvmcopy(p->pagetable, p->kpagetable, 0, p->sz);
···
```

### 3.3 实验中遇到的问题和解决办法
1. `u2kvmcopy()`函数中调用`walk`返回空指针或访问未映射页面导致`panic`:

确保拷贝的地址范围`start`到`end`对应的用户页表都已经被正确映射

2. 在`fork()`、`exec()`、`growproc()`等函数中调用`u2kvmcopy()`后系统崩溃

调用`u2kvmcopy()`后，及时更新进程`sz`字段

### 3.4 实验心得

本实验揭示了页表结构设计在操作系统中的核心地位，通过为每个进程配置独立内核页表，可实现更精细化的内存隔离，防止不同进程间通过内核地址空间越权访问