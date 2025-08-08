<h2 style="text-align:center;font-size:36px;">Lab Page Tables</h2>

# 1. Print a page table (easy)
## 1.1 实验目的

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

## 1.2 实验步骤

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

## 1.3 实验中遇到的问题和解决办法

1. 未判断递归条件导致无限递归:

判断是否为叶节点采用 (pte & (PTE_R|PTE_W|PTE_X)) == 0，确保只对非叶子页表进行递归打印。

2. 未声明函数导致无法运行:

在`kernel/defs.h`中声明`vmprint()`函数
```c
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);
void            vmprint(pagetable_t);
```

## 1.4 实验心得

通过递归遍历页表，能够直观地查看每一级页表及其物理地址映射，极大帮助了对虚拟内存管理的认识

# 2. A kernel page table per process (hard)
## 2.1 实验目的

修改内核来让每一个进程在内核中执行时使用它自己的内核页表的副本。修改`struct proc`来为每一个进程维护一个内核页表，修改调度程序使得切换进程时也切换内核页表

## 2.2 实验步骤

1. 给`kernel/proc.h`里面的`struct proc`加上内核页表的字段:
```c
pagetable_t kernelpt;      // 进程的内核页表
```

2. 在`vm.c`中添加新的方法`proc_kpt_init`，该方法用于在`allocproc` 中初始化进程的内核页表:
```c
void
uvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(pagetable, va, sz, pa, perm) != 0)
    panic("uvmmap");
}

pagetable_t
proc_kpt_init(){
  pagetable_t kernelpt = uvmcreate();
  if (kernelpt == 0) return 0;
  uvmmap(kernelpt, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  uvmmap(kernelpt, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  uvmmap(kernelpt, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  uvmmap(kernelpt, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
  uvmmap(kernelpt, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
  uvmmap(kernelpt, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
  uvmmap(kernelpt, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  return kernelpt;
}
```

3. 在`allocproc`中调用`proc_kpt_init`:
```c
p->kernelpt = proc_kpt_init();
  if(p->kernelpt == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
```

4. 将`procinit`方法中相关的代码迁移到`allocproc`方法中:
```c
// Allocate a page for the process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
char *pa = kalloc();
if(pa == 0)
    panic("kalloc");
uint64 va = KSTACK((int) (p - proc));
kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
p->kstack = va;
```

5. 修改`scheduler()`来加载进程的内核页表到SATP寄存器:
```c
···
p->state = RUNNING;
c->proc = p;

proc_inithart(p->kernelpt);

swtch(&c->context, &p->context);

kvminithart();
···

```

6. 在`freeproc`中释放一个进程的内核页表。首先释放页表内的内核栈:
```c
uvmunmap(p->kernelpt, p->kstack, 1, 1);
p->kstack = 0;
```

7. 在`kernel/proc.c`里面添加一个方法`proc_freekernelpt`:
```c
void
proc_freekernelpt(pagetable_t kernelpt)
{
  // similar to the freewalk method
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = kernelpt[i];
    if(pte & PTE_V){
      kernelpt[i] = 0;
      if ((pte & (PTE_R|PTE_W|PTE_X)) == 0){
        uint64 child = PTE2PA(pte);
        proc_freekernelpt((pagetable_t)child);
      }
    }
  }
  kfree((void*)kernelpt);
}
```

8. 在 `kernel/defs.h` 中声明函数:
```c
void            kvminit(void);
pagetable_t     proc_kpt_init(void); // 用于内核页表的初始化
void            kvminithart(void); 
void            proc_inithart(pagetable_t); // 将进程的内核页表保存到SATP寄存器
```

9. 修改`vm.c`中的`kvmpa`，将原先的`kernel_pagetable`改成`myproc()->kernelpt`:
```c
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(myproc()->kernelpt, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}
```

## 2.3 实验中遇到的问题和解决办法

1. 内核页表访问函数`kvmpa`中未切换到正确页表导致`panic`:

将`kvmpa`中的`kernel_pagetable`替换为`myproc()->kernelpt`，确保当前进程内核页表被使用。

## 2.4 实验心得

本实验通过为每个进程维护独立的内核页表，增强了对多进程虚拟内存管理的理解。内核页表的独立复制有效避免了多个进程共享单一内核页表时可能产生的安全和隔离问题。实现过程中深入体会了页表结构的层次性以及内存映射的细节。

# 3. Simplify copyin/copyinstr (hard)
## 3.1 实验目的

将定义在`kernel/vm.c中的copyin`的主题内容替换为对`copyin_new`的调用；对`copyinstr`和`copyinstr_new`执行相同的操作

## 3.2 实验步骤

1. 首先添加复制函数:
```c
void
u2kvmcopy(pagetable_t pagetable, pagetable_t kernelpt, uint64 oldsz, uint64 newsz){
  pte_t *pte_from, *pte_to;
  oldsz = PGROUNDUP(oldsz);
  for (uint64 i = oldsz; i < newsz; i += PGSIZE){
    if((pte_from = walk(pagetable, i, 0)) == 0)
      panic("u2kvmcopy: src pte does not exist");
    if((pte_to = walk(kernelpt, i, 1)) == 0)
      panic("u2kvmcopy: pte walk failed");
    uint64 pa = PTE2PA(*pte_from);
    uint flags = (PTE_FLAGS(*pte_from)) & (~PTE_U);
    *pte_to = PA2PTE(pa) | flags;
  }
}
```

2. 更改进程的用户映射的每一处 `fork()`, `exec()`, 和`sbrk()`，都复制一份到进程的内核页表:
- `exec()`：

```c
int
exec(char *path, char **argv){
  ...
  sp = sz;
  stackbase = sp - PGSIZE;

  // 添加复制逻辑
  u2kvmcopy(pagetable, p->kernelpt, 0, sz);

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
  ...
}
```

- `fork()`:

```c
int
fork(void){
  ...
  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  ...
  // 复制到新进程的内核页表
  u2kvmcopy(np->pagetable, np->kernelpt, 0, np->sz);
  ...
}
```

- `sbrk()`， 在`kernel/sysproc.c`里面找到`sys_sbrk(void)`，可以知道只有`growproc`是负责将用户内存增加或缩小 n 个字节。防止用户进程增长到超过`PLIC`的地址。

```c
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    // 加上PLIC限制
    if (PGROUNDUP(sz + n) >= PLIC){
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
    // 复制一份到内核页表
    u2kvmcopy(p->pagetable, p->kernelpt, sz - n, sz);
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}
```

3. 替换掉原有的`copyin()`和`copyinstr()`:
```c
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new(pagetable, dst, srcva, len);
}

int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable, dst, srcva, max);
}
```

4. 添加到 `kernel/defs.h` 中:
```c
// vmcopyin.c
int             copyin_new(pagetable_t, char *, uint64, uint64);
int             copyinstr_new(pagetable_t, char *, uint64, uint64);
```

## 3.3 实验中遇到的问题和解决办法
1. `scause = 0xf panic`报错:

确保每次 fork/exec/growproc 后都同步映射内存至 kernelpt

## 3.4 实验心得

本实验揭示了页表结构设计在操作系统中的核心地位，通过为每个进程配置独立内核页表，可实现更精细化的内存隔离，防止不同进程间通过内核地址空间越权访问