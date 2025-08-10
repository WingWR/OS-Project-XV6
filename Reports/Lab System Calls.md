# Lab System Calls

## 1. System call tracing (moderate)
### 1.1 实验目的

创建一个新的`trace`系统调用来控制跟踪

### 1.2 实验步骤

- 修改 `Makefile`:
在 UPROGS 变量中添加:
```bash
  $U_trace\
```

- 定义系统调用编号:
在 `kernel/syscall.h` 文件中添加：
```c
##define SYS_trace 22
```

- 添加系统调用处理函数:
在 `kernel/syscall.c` 文件中添加:
```c
uint64
sys_trace(void)
{
  // 获取系统调用的参数
  argint(0, &(myproc()->trace_mask));
  return 0;
}
```

- 注册系统调用:
在 `kernel/syscall.c` 文件中添加:
```c
extern uint64 sys_trace(void);

static uint64 (*syscalls[])(void) = {
  ...
  [SYS_trace]    sys_trace,
};

static char *syscalls_name[] = {
  ...
  [SYS_trace]    "trace",
};
```

- 在 `proc.h` 中添加字段:
```c
struct proc {
  ...
  int tracemask;
};
```

- 在 `user/user.h` 添加声明:
```c
int trace(int);
```

- 注册系统调用封装函数:
打开 user/usys.pl，在尾部添加：
```c
entry("trace");
```


### 1.3 实验中遇到的问题和解决办法
- 调用 trace 函数时找不到定义:
在 user/usys.pl 中添加 entry("trace");，重新生成用户态系统调用接口。

- 发生`undefined reference to 'trace'`，链接时找不到对应实现。
在内核代码中实现 `sys_trace` 函数，并在 `syscall.c` 中正确声明和注册。

### 1.4 实验心得

通过本次实验，深入理解了 xv6 系统调用的工作流程，包括用户态调用接口的生成、内核系统调用的注册与调度过程。

熟悉了内核数据结构 proc 的扩展和进程控制信息的保存方法。

## 2. Sysinfo (moderate)
### 2.1 实验目的

添加一个系统调用`sysinfo`，它收集有关正在运行的系统的信息

### 2.2 实验步骤

1. 在`Makefile`的`UPROGS`中添加`$U/_sysinfotest`
2. 在`kernel/kalloc.c`中添加一个函数用于获取空闲内存量
```c
void
freebytes(uint64 *dst)
{
  *dst = 0;
  struct run *p = kmem.freelist; // 用于遍历

  acquire(&kmem.lock);
  while (p) {
    *dst += PGSIZE;
    p = p->next;
  }
  release(&kmem.lock);
}
```
3. 在`kernel/proc.c`中添加一个函数获取进程数
```c
void
procnum(uint64 *dst)
{
  *dst = 0;
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state != UNUSED)
      (*dst)++;
  }
}
```
4. 实现`sys_sysinfo`，将数据写入结构体并传递到用户空间
```c
uint64
sys_sysinfo(void)
{
  struct sysinfo info;
  freebytes(&info.freemem);
  procnum(&info.nproc);

  // 获取虚拟地址
  uint64 dstaddr;
  argaddr(0, &dstaddr);

  // 从内核空间拷贝数据到用户空间
  if (copyout(myproc()->pagetable, dstaddr, (char *)&info, sizeof info) < 0)
    return -1;

  return 0;
}
```

5. 在`user/user.h`中声明`sysinfo()`的原型
```c
struct sysinfo;
int sysinfo(struct sysinfo *);
```

### 2.3 实验中遇到的问题和解决办法

1. 没有在 `sysproc.c` 文件中声明 `freebytes` 函数和`procnum`函数:

在 sysproc.c 中添加函数声明:
```c
extern void freebytes(uint64 *dst);
extern void procnum(uint64 *dst);
``` 

2. 用户空间缺少 `sysinfo()` 函数的封装声明:

在 `usys.pl` 中添加 `entry("sysinfo");`，重新生成用户态系统调用接口。

在 `user/user.h` 中添加函数声明：
```c
int sysinfo(struct sysinfo *);
```

3. 宏 `SYS_sysinfo` 未在头文件中定义或未生效:

在 `kernel/syscall.h` 中添加定义:
```c
##define SYS_sysinfo 23
```

### 2.4 实验心得

本实验帮助我理解了如何在 xv6 中设计和实现一个新的系统调用流程，从用户态调用、参数传递、内核态实现再到数据回传。