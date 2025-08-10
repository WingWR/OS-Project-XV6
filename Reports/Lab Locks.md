# Lab Locks

## 1. Memory allocator (moderate)
### 1.1 实验目的

实现每个CPU的空闲列表，并在CPU的空闲列表为空时进行窃取

### 1.2 实验步骤

1. 将`kmem`定义为一个数组，包含`NCPU`个元素，每个CPU对应一个
```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];
```

2. 修改`kinit`，为所有锁初始化以`kmem`开头的名称
```c
void
kinit()
{
  char lockname[8];
  for(int i = 0;i < NCPU; i++) {
    snprintf(lockname, sizeof(lockname), "kmem_%d", i);
    initlock(&kmem[i].lock, lockname);
  }
  freerange(end, (void*)PHYSTOP);
}
```

3. 改`kfree`，使用`cpuid()`和它返回的结果时必须关中断
```c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();  // 关中断
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();  //开中断
}
```

4. 修改`kalloc`，使得在当前CPU的空闲列表没有可分配内存时窃取其他内存的
```c
void *
kalloc(void)
{
  struct run *r;

  push_off();// 关中断
  int id = cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  else {
    int antid;  // another id
    // 遍历所有CPU的空闲列表
    for(antid = 0; antid < NCPU; ++antid) {
      if(antid == id)
        continue;
      acquire(&kmem[antid].lock);
      r = kmem[antid].freelist;
      if(r) {
        kmem[antid].freelist = r->next;
        release(&kmem[antid].lock);
        break;
      }
      release(&kmem[antid].lock);
    }
  }
  release(&kmem[id].lock);
  pop_off();  //开中断

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

### 1.3 实验中遇到的问题和解决办法

1. 获取`cpuid()`前未关闭中断

添加`push_off()`和`pop_off()`，确保CPU一致性

2. 误将`kmem`作为指针处理，导致使用.而非->访问其成员

统一改为kmem[i].lock格式

### 1.4 实验心得

对xv6中内存分配器的设计有了更深入的理解。单一全局锁虽然简单，但不适合多核环境

按CPU划分空闲链表并配合窃取机制，能提升并发性能

## 2. Buffer cache (hard)
### 2.1 实验目的

修改`bget`和`brelse`，以便`bcache`中不同块的并发查找和释放不太可能在锁上发生冲突

### 2.2 实验步骤

1. 定义哈希桶结构，并在`bcache`中删除全局缓冲区链表，改为使用素数个散列桶
```c
##define NBUCKET 13
##define HASH(id) (id % NBUCKET)

struct hashbuf {
  struct buf head;       // 头节点
  struct spinlock lock;  // 锁
};

struct {
  struct buf buf[NBUF];
  struct hashbuf buckets[NBUCKET];  // 散列桶
} bcache;
```

2. 在`binit()`中进行初始化
```c
void
binit(void) {
  struct buf* b;
  char lockname[16];

  for(int i = 0; i < NBUCKET; ++i) {
    // 初始化散列桶的自旋锁
    snprintf(lockname, sizeof(lockname), "bcache_%d", i);
    initlock(&bcache.buckets[i].lock, lockname);

    // 初始化散列桶的头节点
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  // Create linked list of buffers
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    // 利用头插法初始化缓冲区列表,全部放到散列桶0上
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}
```

3. 更改`brelse`，不再获取全局锁
```c
void
brelse(struct buf* b) {
  if(!holdingsleep(&b->lock))
    panic("brelse");

  int bid = HASH(b->blockno);

  releasesleep(&b->lock);

  acquire(&bcache.buckets[bid].lock);
  b->refcnt--;

  // 更新时间戳
  // 由于LRU改为使用时间戳判定，不再需要头插法
  acquire(&tickslock);
  b->timestamp = ticks;
  release(&tickslock);

  release(&bcache.buckets[bid].lock);
}
```

4. 更改`bget`，当没有找到指定的缓冲区时进行分配，分配方式是优先从当前列表遍历，找到一个没有引用且`timestamp`最小的缓冲区，如果没有就申请下一个桶的锁，并遍历该桶，找到后将该缓冲区从原来的桶移动到当前桶中

```c
static struct buf*
bget(uint dev, uint blockno) {
  struct buf* b;

  int bid = HASH(blockno);
  acquire(&bcache.buckets[bid].lock);

  // Is the block already cached?
  for(b = bcache.buckets[bid].head.next; b != &bcache.buckets[bid].head; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;

      // 记录使用时间戳
      acquire(&tickslock);
      b->timestamp = ticks;
      release(&tickslock);

      release(&bcache.buckets[bid].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  b = 0;
  struct buf* tmp;

  // Recycle the least recently used (LRU) unused buffer.
  // 从当前散列桶开始查找
  for(int i = bid, cycle = 0; cycle != NBUCKET; i = (i + 1) % NBUCKET) {
    ++cycle;
    // 如果遍历到当前散列桶，则不重新获取锁
    if(i != bid) {
      if(!holding(&bcache.buckets[i].lock))
        acquire(&bcache.buckets[i].lock);
      else
        continue;
    }

    for(tmp = bcache.buckets[i].head.next; tmp != &bcache.buckets[i].head; tmp = tmp->next)
      // 使用时间戳进行LRU算法，而不是根据结点在链表中的位置
      if(tmp->refcnt == 0 && (b == 0 || tmp->timestamp < b->timestamp))
        b = tmp;

    if(b) {
      // 如果是从其他散列桶窃取的，则将其以头插法插入到当前桶
      if(i != bid) {
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache.buckets[i].lock);

        b->next = bcache.buckets[bid].head.next;
        b->prev = &bcache.buckets[bid].head;
        bcache.buckets[bid].head.next->prev = b;
        bcache.buckets[bid].head.next = b;
      }

      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;

      acquire(&tickslock);
      b->timestamp = ticks;
      release(&tickslock);

      release(&bcache.buckets[bid].lock);
      acquiresleep(&b->lock);
      return b;
    } else {
      // 在当前散列桶中未找到，则直接释放锁
      if(i != bid)
        release(&bcache.buckets[i].lock);
    }
  }

  panic("bget: no buffers");
}
```

### 2.3 实验中遇到的问题和解决办法

1. `bget`中重新分配持有会两个锁，如果桶a持有自己的锁，再申请桶b的锁，与此同时如果桶b持有自己的锁，再申请桶a的锁就会造成死锁:

使用了`if(!holding(&bcache.bucket[i].lock))`来进行检查。此外，代码优先从自己的桶中获取缓冲区

2. 在`release`桶的锁并重新`acquire`的这段时间，另一个CPU可能也以相同的参数调用了`bget`，也发现没有该缓冲区并想要执行分配。导致`usertests`中的`manywrites`测试报错

先释放了散列桶的锁之后再重新获取，之所以这样做是为了让所有代码都保证申请锁的顺序
```c
// 1. 第一次查找，没有找到 block 对应的 buffer
release(&bcache.buckets[bid].lock);
// 2. 获取全局锁（此时有分配全局 buffer 的唯一权）
acquire(&bcache.lock);
// 3. 再次获取桶锁
acquire(&bcache.buckets[bid].lock);
// 4. 再次查找桶中是否已经有人插入了该 block
for (b = bcache.buckets[bid].head; b; b = b->next) {
  if (b->dev == dev && b->blockno == blockno) {
    b->refcnt++;
    release(&bcache.lock);
    release(&bcache.buckets[bid].lock);
    return b;
  }
}
```

### 2.4 实验心得

`xv6` 的 `buffer cache` 是用一个全局的链表和锁管理所有缓冲区，这在单核环境下简单有效，但在多核并发环境下会成为瓶颈，也容易引发数据竞争。

理解了操作系统中缓存一致性和并发控制的重要性