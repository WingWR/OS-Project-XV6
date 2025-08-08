<h2 style="text-align:center;font-size:36px;">Lab Multithreading</h2>

# 1. Uthread: switching between threads (moderate)
## 1.1 实验目的

提出一个创建线程和保存/恢复寄存器以在线程之间切换的计划，并实现该计划

## 1.2 实验步骤

1. 给进程加入上下文属性:
```c
struct Context{
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct thread {
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  struct Context ctx;
};
```

2. 完成`thread_switch`函数
```c
.text

/*
* save the old thread's registers,
* restore the new thread's registers.
*/

.globl thread_switch
thread_switch:
    /* YOUR CODE HERE */
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)
    ret    /* return to ra */
```

3. 修改`thread_scheduler`，添加线程切换语句:
```c
if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
    thread_switch((uint64)&t->context, (uint64)&current_thread->context);
  } else
    next_thread = 0;
```

4. 在`thread_create`中对`thread`结构体做初始化
```c
void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE
  t->context.ra = (uint64)func;                   // 设定函数返回地址
  t->context.sp = (uint64)t->stack + STACK_SIZE;  // 设定栈指针
}
```

## 1.3 实验中遇到的问题和解决办法

未遇到特别问题

## 1.4 实验心得

本实验主要实现了用户级线程的切换机制，核心在于保存和恢复线程的寄存器上下文，通过 `thread_switch` 完成线程切换。在实现过程中，最大的收获是加深了对寄存器保存/恢复和线程栈初始化的理解，对线程调度、上下文切换有了更深刻的认识

# 2. Using threads (moderate)
## 2.1 实验目的

通过使用线程和锁机制，解决多线程访问哈希表的并发安全问题并提升程序的并行性能。

## 2.2 实验步骤

- 修改`put()`和`get()`函数，给操作上锁:
```c
static 
void put(int key, int value)
{
  int i = key % NBUCKET;

  pthread_mutex_lock(&bkt_lock[i]);
  // is the key already present?
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
  pthread_mutex_unlock(&bkt_lock[i]);
}

static struct entry*
get(int key)
{
  int i = key % NBUCKET;

  pthread_mutex_lock(&bkt_lock[i]);
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key) break;
  }
  pthread_mutex_unlock(&bkt_lock[i]);
  return e;
}
```

## 2.3 实验中遇到的问题和解决办法

未遇到特别问题

## 2.4 实验心得

通过本次实验，我对多线程编程中的并发问题有了更直观的认识。最初由于多个线程同时访问和修改哈希表，导致键值丢失，体现了线程安全的重要性。通过在`put()`和`get()`中引入锁机制，保证了数据一致性，成功消除了丢失的键。

# 3. Barrier(moderate)
## 3.1 实验目的

实现基于条件变量的线程同步屏障，确保所有线程在同一轮次到达屏障后再共同继续执行。

## 3.2 实验步骤

- 完成`barrier()`函数:
```c
static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  pthread_mutex_lock(&bstate.barrier_mutex);
  bstate.nthread++;
  if(bstate.nthread < nthread){
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }else{ 
    bstate.nthread = 0;
    bstate.round++;
    pthread_cond_broadcast(&bstate.barrier_cond);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}
```

## 3.3 实验中遇到的问题和解决办法

- 未考虑栈顶导致地址越界:

更正栈顶指针:
```c
t->ctx.sp = (uint64)t->stack + STACK_SIZE ;  // 设定栈指针
```

## 3.4 实验心得

通过本次实验，我掌握了利用条件变量和互斥锁实现多线程同步屏障的方法，加深了对线程协作机制的理解。