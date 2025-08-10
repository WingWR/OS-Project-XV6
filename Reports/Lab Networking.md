# Lab Networking

## Your Job (hard)
### 1 实验目的

在`kernel/e1000.c`中完成`e1000_transmit()`和`e1000_recv()`，以便驱动程序可以发送和接收数据包

### 2 实验步骤

1. 完成`e1000_transmit()`函数，用来发送
```c
int
e1000_transmit(struct mbuf *m)
{
  acquire(&e1000_lock);
  // 查询ring里下一个packet的下标
  int idx = regs[E1000_TDT];

  if ((tx_ring[idx].status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);
    return -1;
  }

  // 释放上一个包的内存
  if (tx_mbufs[idx])
    mbuffree(tx_mbufs[idx]);

  tx_mbufs[idx] = m;
  tx_ring[idx].length = m->len;
  tx_ring[idx].addr = (uint64) m->head;
  tx_ring[idx].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;

  release(&e1000_lock);
  return 0;
}
```

2. 完成`e1000_recv()`函数，用来接收
```c
static void
e1000_recv(void)
{
  while (1) {
    // 把所有到达的packet向上层递交
    int idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    if ((rx_ring[idx].status & E1000_RXD_STAT_DD) == 0) {
      return;
    }
    rx_mbufs[idx]->len = rx_ring[idx].length;
    // 向上层network stack传输
    net_rx(rx_mbufs[idx]);
    
    rx_mbufs[idx] = mbufalloc(0);
    rx_ring[idx].status = 0;
    rx_ring[idx].addr = (uint64)rx_mbufs[idx]->head;
    regs[E1000_RDT] = idx;
  }
}
```

3. 在`MakeFile`中加入`nettests`
```bash
$U/_nettests\
```

### 3 实验中遇到的问题和解决办法

1. 程序能够发送数据包，但收不到响应，网络通信卡在等待阶段。

在 `e1000_recv` 中及时调用 `mbufalloc()` 分配新的缓冲区，重置描述符状态

设置发送命令时包含 E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP 标志，确保硬件完成后通知驱动

2. 描述符中 `addr` 字段为何用物理地址？为什么要强制转换指针

驱动需要把内存缓冲区的虚拟地址转换成物理地址，传递给网卡DMA控制器。因为网卡直接访问物理内存，不能识别虚拟地址

### 4 实验心得

本次实验深入理解了以太网驱动的发送与接收流程，特别是环形缓冲区的管理和硬件寄存器的操作细节