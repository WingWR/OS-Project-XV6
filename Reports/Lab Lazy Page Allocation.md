<h2 style="text-align:center;font-size:36px;">Lab Lazy Page Allocation</h2>

# 1. Eliminate allocation from sbrk() (easy)
## 1.1 实验目的

删除`sbrk(n)`系统调用中的页面分配代码，新的`sbrk(n)`应该只将进程的大小增加`n`，然后返回旧的大小

## 1.2 实验步骤

- 修改`sbrk()`函数:
```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  // lazy allocation
  myproc()->sz += n;

  return addr;
}
```

## 1.3 实验中遇到的问题和解决办法

未遇到什么问题

## 1.4 实验心得

理解了`Lazy Allocation`的概念，为后面做准备

# 2. Lazy allocation (moderate)
## 2.1 实验目的

修改`trap.c`中的代码以响应来自用户空间的页面错误，方法是新分配一个物理页面并映射到发生错误的地址，然后返回到用户空间，让进程继续执行

## 2.2 实验步骤

1. 修改`usertrap()`函数，如果是 `13` 或 `15` 就进行下一步的处理:
```c
else if(cause == 13 || cause == 15) {
    // 处理页面错误
    uint64 fault_va = r_stval();  // 产生页面错误的虚拟地址
    char* pa;                     // 分配的物理地址
    if(PGROUNDUP(p->trapframe->sp) - 1 < fault_va && fault_va < p->sz &&
      (pa = kalloc()) != 0) {
        memset(pa, 0, PGSIZE);
        if(mappages(p->pagetable, PGROUNDDOWN(fault_va), PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_X | PTE_U) != 0) {
          kfree(pa);
          p->killed = 1;
        }
    } else {
      // printf("usertrap(): out of memory!\n");
      p->killed = 1;
    }
  }
```

2. 修改`uvmunmap()`防止系统发生`panic`
```c
for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      continue;
```

## 2.3 实验中遇到的问题和解决办法

暂时未遇见问题

## 2.4 实验心得

1. 对`usertrap()`中缺页异常的判断和处理逻辑：当出现页面错误时，需要根据`stval`找到出错的虚拟地址，并在合法范围内为其分配新的物理页并映射

2. 修改`uvmunmap()`时需要注意跳过未映射的页，否则会因为访问空指针而`panic`

# 3. Lazytests and Usertests (moderate)
## 3.1 实验目的

修改 `xv6` 内核代码，使得它能够通过 `lazytests` 和 `usertests`，即保证惰性内存分配机制在各种复杂情况下都能正常运行

## 3.2 实验步骤

1. 考虑`sbrk`中参数可以为负数的问题，进行修改:
```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *p = myproc();
  if(argint(0, &n) < 0)
    return -1;
  addr = p->sz;
  if(n < 0){
    if(p->sz + n < 0){ // 一个进程不能释放比自己大的空间
      return -1;
    }
    if(growproc(n) < 0){
      // 注意这里是实际调用 growproc 去释放空间的。
      printf("growproc err\n");
      return -1;
    }
  }else{
    myproc()->sz += n;
  }
  // if(growproc(n) < 0) 
  //   return -1;
  return addr;
}
```

2. 如果一个进程出现缺页错误的地址以前并没有被分配过。那么我们就不去分配这个页，而是直接把进程 `kill`:
- 用函数判断虚拟地址是否合法:
```c
int is_lazy_addr(uint64 va){
  struct proc *p = myproc();
  if(va < PGROUNDDOWN(p->trapframe->sp)
  && va >= PGROUNDDOWN(p->trapframe->sp) - PGSIZE
  ){
    // 防止 guard page，这个之后会提到
    return 0;
  }
  if(va > MAXVA){
    return 0;
  }
  pte_t* pte = walk(p->pagetable, va, 0);
  
  if(pte && (*pte & PTE_V)){
    return 0;
  }  

  if(va >= p->sz){
    return 0;
  }

  return 1;
}
```

3. 正确的处理 `fork()` 中从父进程到子进程的内存拷贝:
```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;
    if((*pte & PTE_V) == 0)
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

4. 如果分配物理页的时候，没有足够内存了，应该把当前进程 kill 
```c
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  if(is_lazy_addr(va)){ // 注意这里，如果是懒分配的会先分配物理地址。
    lazy_alloc(va);
  }
  pte = walk(pagetable, va, 0);

  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}
```

5. 正确处理发生在用户栈下面地址的缺页错误

## 3.3 实验中遇到的问题和解决办法

- 自定义的函数`is_lazy_addr(uint64 va)`和`int lazy_alloc(uint64 va)`无法在其他文件中调用:

将函数声明在`defs.h`中，可以全局调用

## 3.4 实验心得

通过本次实验，对`Lazy Allocation`的机制和内核内存管理有了更深入的理解。在实现过程中，需要考虑各种情况，例如 `sbrk` 参数为负数时释放内存、`fork` 时的内存拷贝、缺页异常的处理以及内核在访问未分配页时的应对