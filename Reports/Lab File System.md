# Lab File System

## 1. Large files (moderate)
### 1.1 实验目的

增加xv6文件的最大大小

### 1.2 实验步骤

1. 在`fs.h`中添加宏定义
```c
// 文件系统相关
##define NDIRECT 11
##define NINDIRECT (BSIZE / sizeof(uint))
##define NDINDIRECT ((BSIZE / sizeof(uint)) * (BSIZE / sizeof(uint)))
##define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT)
##define NADDR_PER_BLOCK (BSIZE / sizeof(uint))  // 一个块中的地址数量
```

2. 修改`inode`结构体中`addrs`元素数量
```c
// fs.h
struct dinode {
  ...
  uint addrs[NDIRECT + 2];   // Data block addresses
};

// file.h
struct inode {
  ...
  uint addrs[NDIRECT + 2];
};
```

3. 修改`bmap`支持二级索引
```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp, *bp2; 

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  bn -= NINDIRECT;

  if(bn < NBI_INDIRECT){
    if((addr = ip->addrs[NDIRECT + 1]) == 0) 
      ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);    
    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    uint idx_b1 = bn / NINDIRECT;
    if((addr = a[idx_b1]) == 0){ // 一个一级块负责 256 个二级块，这里检测对应一级块是否存在
      a[idx_b1] = addr = balloc(ip->dev);
      log_write(bp); 
    } 
    brelse(bp);
    
    bp2 = bread(ip->dev, addr); // bp2 为二级块的缓存
    a = (uint *)bp2->data;
    uint idx_b2 = bn % NINDIRECT;
    if((addr = a[idx_b2]) == 0){
      a[idx_b2] = addr = balloc(ip->dev);
      log_write(bp2);
    }
    brelse(bp2);
    return addr;
  }
  
  panic("bmap: out of range");
}
```

4. 修改`itrunc`释放所有块
```c
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }
  
  if(ip->addrs[NDIRECT + 1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint*)bp->data;
    for (i = 0; i < NINDIRECT; i++){
      if(a[i]){
        struct buf* bp2 = bread(ip->dev, a[i]); 
        uint *a2 = bp2->data;
        for(j = 0; j < NINDIRECT; j++){
          if(a2[j])
            bfree(ip->dev, a2[j]);
        } 

        brelse(bp2);
        bfree(ip->dev, a[i]);
        // 和 a[i] 对应的是 bp2
        // a[i] 是块号，bp2 是实际的块缓存
      }      
    }
    brelse(bp); // 释放缓存
    bfree(ip->dev, ip->addrs[NDIRECT + 1]); // 释放磁盘块
    ip->addrs[NDIRECT + 1] = 0; // 不是 + 1
  }
  ip->size = 0;
  iupdate(ip);
}
```

### 1.3 实验中遇到的问题和解决办法

1. `uint *a2 = bp2->data;`导致类型不匹配警告:
 
使用强制类型转换`uint *a2 = (uint *)bp2->data;`，消除类型不匹配

2. 在计算一级和二级索引时，注意避免索引越界:

修改`NDIRECT`、`NINDIRECT`和`NDINDIRECT`宏定义，避免索引计算错误

### 1.4 实验心得

理解了多级间接索引在文件系统中扩展文件大小的实现机制。修改`bmap`函数支持二级索引，使得文件可以存储更多数据块，极大提升了文件系统的容量

## 2. Symbolic links (moderate)
### 2.1 实验目的

实现`symlink(char *target, char *path)`系统调用，该调用在引用由`target`命名的文件的路径处创建一个新的符号链接

### 2.2 实验步骤

1. 添加 `O_NOFOLLOW` 的标志位，打开软连接本身
```c
##define O_RDONLY  0x000
##define O_WRONLY  0x001
##define O_RDWR    0x002
##define O_CREATE  0x200
##define O_TRUNC   0x400
##define O_NOFOLLOW 0x004
```

2. 注册系统调用`sys_symlink()`
```c
uint64 sys_symlink(void) {
  char target[MAXPATH], path[MAXPATH];
  struct inode *ip;
  int n;

  if ((n = argstr(0, target, MAXPATH)) < 0
    || argstr(1, path, MAXPATH) < 0) {
    return -1;
  }

  begin_op();
  // create the symlink's inode
  if((ip = create(path, T_SYMLINK, 0, 0)) == 0) {
    end_op();
    return -1;
  }
  // write the target path to the inode
  if(writei(ip, 0, (uint64)target, 0, n) != n) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}
```

3. 修改`sys_open()`函数
```c
···
if(ip->type == T_SYMLINK && (omode & O_NOFOLLOW) == 0) {
    if((ip = follow_symlink(ip)) == 0) {
      end_op();
      return -1;
    }
  }
···
```

### 2.3 实验中遇到的问题和解决办法

1. 软链接之间存在循环引用，导致系统崩溃

在 `follow_symlink()` 函数中加入最大递归深度限制

### 2.4 实验心得

通过本次实验，理解了文件系统中软链接的实现原理