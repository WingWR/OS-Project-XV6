<h2 style="text-align:center;font-size:36px;">Traps</h2>

# 1. RISC-V assembly (easy)
## 1.1 实验目的

阅读`call.asm`中函数`g`、`f`和`main`的代码，回答一些问题。

## 1.2 实验步骤

**1. Which registers contain arguments to functions? For example, which register holds 13 in main's call to printf?**

- **Answer:** 在`a0-a7`中存放参数，`13`存放在`a2`中 

**2. Where is the call to function `f` in the assembly code for main? Where is the call to `g`? (Hint: the compiler may inline functions.)**

- **Answer:** 在C代码中，`main`调用`f`，`f`调用`g`。而在生成的汇编中，`main`函数进行了内联优化处理

**3. At what address is the function `printf` located?**

- **Answer:** 在`0x630`

**4. What value is in the register ra just after the jalr to printf in main?**

- **Answer:** 执行此行代码后，将跳转到`printf`函数执行，并将`PC+4=0X34+0X4=0X38`保存到`ra`中

**5. Run the following code.**
```c
unsigned int i = 0x00646c72;
printf("H%x Wo%s", 57616, &i);
```      

**What is the output?**

- **Answer:**输出为：`HE110 World`

**If the RISC-V were instead big-endian what would you set i to in order to yield the same output? Would you need to change 57616 to a different value?**

- **Answer:** 若为大端存储，`i`应改为`0x726c6400`，不需改变`57616`

**6. In the following code, what is going to be printed after `y=`? (note: the answer is not a specific value.) Why does this happen?**
- `printf("x=%d y=%d", 3);`

- **Answer:** 原本需要两个参数，却只传入了一个，因此`y=`后面打印的结果取决于之前`a2`中保存的数据 

## 1.3 实验中遇到的问题和解决办法

未遇到问题

## 1.4 实验心得

通过本次实验，我对 RISC-V 汇编函数调用规范 和 参数传递方式 有了更直观的认识，从底层角度理解了 C 语言与汇编之间的联系，并让我更加熟悉了寄存器使用规范和编译器优化对汇编代码的影响

# 2. Backtrace(moderate)
## 2.1 实验目的

在`kernel/printf.c`中实现名为`backtrace()`的函数

## 2.2 实验步骤

1. 在`kernel/printf.c`中实现名为`backtrace()`的函数
```c
void
backtrace(void) {
  printf("backtrace:\n");
  // 读取当前帧指针
  uint64 fp = r_fp();
  while (PGROUNDUP(fp) - PGROUNDDOWN(fp) == PGSIZE) {
    // 返回地址保存在-8偏移的位置
    uint64 ret_addr = *(uint64*)(fp - 8);
    printf("%p\n", ret_addr);
    // 前一个帧指针保存在-16偏移的位置
    fp = *(uint64*)(fp - 16);
  }
}
```

2. 在`kernel/defs.h`中添加`backtrace`的原型

3. 在`kernel/riscv.h`中添加
```c
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r" (x) );
  return x;
}
```

## 2.3 实验中遇到的问题和解决办法

1. `r_fp()`未声明:

在 `kernel/riscv.h` 中定义 `r_fp()`

2. 未成功调用`backtrace()`:

在 `kernel/sysproc.c`中调用`backtrace()`

## 2.4 实验心得

深入理解了` RISC-V `函数调用栈帧结构 及其回溯原理。在实现 `backtrace()` 的过程中，我学习了如何利用帧指针`fp`在内核态中遍历调用栈，并逐层打印出返回地址，从而直观展示了函数的调用路径。

# 3. Alarm(Hard)
## 3.1 实验目的

向`XV6`添加一个特性，在进程使用CPU的时间内，XV6定期向进程发出警报

## 3.2 实验步骤

1. 在`user.h`中添加函数声明:
```c
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
```

2. 在`sysproc.c`中定义函数:
```c
uint64
sys_sigalarm(void)
{
	int ticks;
	uint64 handler;
	struct proc *p = myproc();
	if(argint(0, &ticks) < 0 || argaddr(1, &handler) < 0)
	return -1;
	p->alarminterval = ticks;
	p->alarmhandler = (void (*)())handler;
	p->alarmticks = 0;
	return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  p->sigreturned = 1;
  *(p->trapframe) = p->alarmtrapframe;
  usertrapret();
  return 0;
}
```

3. 初始化警告字段

4. 修改`usertrap()`函数
```c
if(which_dev == 2)
  {
    p->alarmticks += 1;
    if ((p->alarmticks >= p->alarminterval) && (p->alarminterval > 0))
    {
      p->alarmticks = 0;
      if (p->sigreturned == 1)
      {
        p->alarmtrapframe = *(p->trapframe);
        p->trapframe->epc = (uint64)p->alarmhandler;
        p->sigreturned = 0;
        usertrapret();
      }
    }
    yield();
  }
```

## 3.3 实验中遇到的问题和解决办法

1. 未声明`alarmtest`:

在`MakeFile`中添加`$U_alarmtest\`

2. 未声明`sigalarm`和`sigreturn`:

在`user.h`中添加`sigalarm`和`sigreturn`

## 3.4 实验心得

最后这个Alarm实验部分还是难度非常大，用了很久时间才完成，很多地方也并没有完全理解。但是通过完成实验的过程，
了解了时钟中断是怎么让内核定时介入用户进程的，同时还学会了保存和恢复进程状态，必须保存 `trapframe`，并在 `sigreturn()` 中恢复