<h2 style="text-align:center;font-size:36px;">Lab Mmap</h2>

# mmap (hard)
## 1 实验目的

实现一个 `UNIX` 操作系统中常见系统调用 `mmap()` 和 `munmap()` 的子集。此系统调用会把文件映射到用户空间的内存，这样用户可以直接通过内存来修改和访问文件

## 2 实验步骤

1. 在 `Makefile` 中添加 `$U/_mmaptest`

2. 添加有关 `mmap` 和 `munmap` 系统调用的定义声明

3. 在 `kernel/proc.h` 中定义 `vm_area` 结构体
```c
struct vm_area {
  int used;           // 是否已被使用
  uint64 addr;        // 起始地址
  int len;            // 长度
  int prot;           // 权限
  int flags;          // 标志位
  int vfd;            // 对应的文件描述符
  struct file* vfile; // 对应文件
  int offset;         // 文件偏移，本实验中一直为0
};
```

4. 在`allocproc`中初始化`vma`数组

5. 完成`mmap()`函数的实现
```c
uint64
sys_mmap(void) {
  uint64 addr;
  int length;
  int prot;
  int flags;
  int vfd;
  struct file* vfile;
  int offset;
  uint64 err = 0xffffffffffffffff;

  // 获取系统调用参数
  if(argaddr(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &prot) < 0 ||
    argint(3, &flags) < 0 || argfd(4, &vfd, &vfile) < 0 || argint(5, &offset) < 0)
    return err;

  if(addr != 0 || offset != 0 || length < 0)
    return err;

  if(vfile->writable == 0 && (prot & PROT_WRITE) != 0 && flags == MAP_SHARED)
    return err;

  struct proc* p = myproc();
  // 没有足够的虚拟地址空间
  if(p->sz + length > MAXVA)
    return err;

  // 遍历查找未使用的VMA结构体
  for(int i = 0; i < NVMA; ++i) {
    if(p->vma[i].used == 0) {
      p->vma[i].used = 1;
      p->vma[i].addr = p->sz;
      p->vma[i].len = length;
      p->vma[i].flags = flags;
      p->vma[i].prot = prot;
      p->vma[i].vfile = vfile;
      p->vma[i].vfd = vfd;
      p->vma[i].offset = offset;

      // 增加文件的引用计数
      filedup(vfile);

      p->sz += length;
      return p->vma[i].addr;
    }
  }

  return err;
}
```

6. 完成`munmap()`函数的实现
```c
uint64
sys_munmap(void) {
  uint64 addr;
  int length;
  if(argaddr(0, &addr) < 0 || argint(1, &length) < 0)
    return -1;

  int i;
  struct proc* p = myproc();
  for(i = 0; i < NVMA; ++i) {
    if(p->vma[i].used && p->vma[i].len >= length) {
      // 根据提示，munmap的地址范围只能是
      // 1. 起始位置
      if(p->vma[i].addr == addr) {
        p->vma[i].addr += length;
        p->vma[i].len -= length;
        break;
      }
      // 2. 结束位置
      if(addr + length == p->vma[i].addr + p->vma[i].len) {
        p->vma[i].len -= length;
        break;
      }
    }
  }
  if(i == NVMA)
    return -1;

  // 将MAP_SHARED页面写回文件系统
  if(p->vma[i].flags == MAP_SHARED && (p->vma[i].prot & PROT_WRITE) != 0) {
    filewrite(p->vma[i].vfile, addr, length);
  }

  // 判断此页面是否存在映射
  uvmunmap(p->pagetable, addr, length / PGSIZE, 1);

  // 当前VMA中全部映射都被取消
  if(p->vma[i].len == 0) {
    fileclose(p->vma[i].vfile);
    p->vma[i].used = 0;
  }

  return 0;
}
```

7. 修改`exit()`，将进程的已映射区域取消映射
```c
···
for(int i = 0; i < NVMA; ++i) {
    if(p->vma[i].used) {
      if(p->vma[i].flags == MAP_SHARED && (p->vma[i].prot & PROT_WRITE) != 0) {
        filewrite(p->vma[i].vfile, p->vma[i].addr, p->vma[i].len);
      }
      fileclose(p->vma[i].vfile);
      uvmunmap(p->pagetable, p->vma[i].addr, p->vma[i].len / PGSIZE, 1);
      p->vma[i].used = 0;
    }
  }
···
```

8. 修改`fork()`，复制父进程的`VMA`并增加文件引用计数
```c
···
for(i = 0; i < NVMA; ++i) {
    if(p->vma[i].used) {
      memmove(&np->vma[i], &p->vma[i], sizeof(p->vma[i]));
      filedup(p->vma[i].vfile);
    }
  }
···
```

## 3 实验中遇到的问题和解决办法

1. 未在 `fork()` 函数中复制 `VMA`，会导致子进程没有对应的映射信息，从而访问非法地址

在 `fork()` 中遍历父进程的 `vma` 数组并使用 `memmove()` 拷贝至子进程，同时对映射文件调用 `filedup()` 增加引用计数

2. 在定义 `struct file` 时，`sleeplock` 被报为不完整类型

在 `file.h` 文件头部加入 `#include "sleeplock.h"`

3. 在解除映射时没有正确调用 `uvmunmap()`，导致错误

调用 `uvmunmap(p->pagetable, addr, length / PGSIZE, 1)` 正确解除映射

## 4 实验心得

通过本次实验，对 `mmap()` 与 `munmap()` 系统调用的底层实现有了深入理解。实验过程中，我体会到了操作系统如何将文件映射到用户空间，并通过页表实现按需访问