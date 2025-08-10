# Lab Utilities

## 1. Boot xv6 (easy)
### 1.1 实验目的
获取实验室的xv6源代码，配置实验的环境

### 1.2 实验步骤

- 配置在Ubuntu下的实验环境
```bash
sudo apt-get install git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu 
$ sudo apt-get remove qemu-system-misc
$ sudo apt-get install qemu-system-misc=1:4.2-3ubuntu6
```

- 获取实验室的xv6源代码

```bash
$ git clone git://g.csail.mit.edu/xv6-labs-2020
$ cd xv6-labs-2020
$ git checkout util
```

- 构建并运行xv6

```bash
$ make qemu
```

### 1.3 实验中遇到的问题和解决办法
实验无问题

### 1.4 实验心得
配环境太折磨了

## 2. sleep (easy)
### 2.1 实验目的
实现xv6的程序sleep, sleep应该达到用户指定的计时数，一个计时数是由xv6内核定义的时间概念，即来自定时器芯片的两个中断之间的时间

### 2.2 实验步骤
- 在`user\user.h`中找到了`int sleep(int);`的声明

说明`sleep`接受一个`int`型整数的参数

- 编写文件`sleep.c`实现函数`int sleep(int)`

```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char const *argv[])
{
  if (argc != 2) { 
    fprintf(2, "usage: sleep <time>\n");
    exit(1);
  }

  sleep(atoi(argv[1]));
  exit(0);
}

```

### 2.3 实验中遇到的问题和解决办法

在编写代码时要先检测输入的数量，因为没有检测输入数量遇到错误。加上之后能正常运行


### 2.4 实验心得

加深了对 xv6 系统调用机制的理解

## 3. pingpong (easy)
### 3.1 实验目的

编写一个使用系统调用的程序来在两个进程之间“ping-pong”一个字节

### 3.2 实验步骤

- 在子进程中，分别关闭两个管道的读端口和写端口，此时子进程通过p2管道的读端口p2[0]来获取一比特信息(read())，如果得到，则打印: received ping。

- 在父进程中，分别关闭c2p，p2c两个管道的写端口和读端口，并将缓冲区buf中的内容写入p2c的写端口(write())。此时若能够从c2p的读端口读取一比特数据，则打印: received pong

- 编写文件`pingpong.c`,验证通信机制
  
```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char const *argv[])
{
    ##define RD 0 //pipe的read端
    ##define WR 1 //pipe的write端

    char buf = 'P'; //传送字节
    int exit_status = 0;    //错误判断

    int p2c[2], c2p[2]; //创建双管道
    pipe(p2c);
    pipe(c2p);

    int pid = fork();
    if(pid == 0){   //子进程
        close(p2c[WR]);
        close(c2p[RD]);

        if (read(p2c[RD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "child read() error!\n");
            exit_status = 1; //标记出错
        } else {
            fprintf(1, "%d: received ping\n", getpid());
        }

        if (write(c2p[WR], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "child write() error!\n");
            exit_status = 1;
        }

        close(p2c[RD]);
        close(c2p[WR]);

        exit(exit_status);
    }
    else{
        close(p2c[RD]);
        close(c2p[WR]);

        if (write(p2c[WR], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "parent write() error!\n");
            exit_status = 1;
        }

        if (read(c2p[RD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "parent read() error!\n");
            exit_status = 1; //标记出错
        } else {
            fprintf(1, "%d: received pong\n", getpid());
        }

        close(p2c[WR]);
        close(c2p[RD]);

        exit(exit_status);
    }

}
```

### 3.3 实验中遇到的问题和解决办法

管道操作不知道如何实现，学习了相关的系统调用

```bash
​ a. fork系统调用：用于创建子进程；
​ b. pipe系统调用：用于创建管道；
​ c. read系统调用：用于读取管道：
​ d. write系统调用：用于写管道；
​ e. getpid系统调用：用于查询进程号；
```

### 3.4 实验心得

验证了验证Xv6的部分进程通讯机制

## 4. primes (moderate)/(hard)
### 4.1 实验目的

使用管道编写prime sieve(筛选素数)的并发版本

### 4.2 实验步骤

- 使用 pipe(p) 创建初始管道，父进程将 2~35 写入。
- 子进程执行 primes(p)，从管道中读取第一个数作为当前素数，并打印该素数
- 为剩余数创建一个新的管道 pipe(p2)；
- 把不能被该素数整除的数写入新管道；
- 然后再为新管道递归创建一个子进程，继续筛选。
- 编写文件`primes.c`

```c
void primes(int lpipe[2])
{
  close(lpipe[WR]);
  int first;
  if (lpipe_first_data(lpipe, &first) == 0) {
    int p[2];
    pipe(p); // 当前的管道
    transmit_data(lpipe, p, first);

    if (fork() == 0) {
      primes(p);    
    } else {
      close(p[RD]);
      wait(0);
    }
  }
  exit(0);
}
```

### 4.3 实验中遇到的问题和解决办法

如果子进程没有关闭写端或父进程没关读端，read 永远等不到 EOF。

- 在子进程中需要关闭写端，父进程中要关闭读端

### 4.4 实验心得

理解了 xv6 中的 pipe() 与 fork() 的协同工作机制

## 5. find（难度：Moderate）
### 5.1 实验目的

写一个简化版本的`find`程序,查找目录树中具有特定名称的所有文件

### 5.2 实验步骤

- 浏览学习user/ls.c中读目录的方法
- 在目录中递归寻找特定名字的文件
- 编写`find.c`

```c
void find(char *path, const char *filename)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot fstat %s\n", path);
    close(fd);
    return;
  }

  //参数错误，find的第一个参数必须是目录
  if (st.type != T_DIR) {
    fprintf(2, "usage: find <DIRECTORY> <filename>\n");
    return;
  }

  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
    fprintf(2, "find: path too long\n");
    return;
  }
  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/'; //p指向最后一个'/'之后
  while (read(fd, &de, sizeof de) == sizeof de) {
    if (de.inum == 0)
      continue;
    memmove(p, de.name, DIRSIZ); //添加路径名称
    p[DIRSIZ] = 0;               //字符串结束标志
    if (stat(buf, &st) < 0) {
      fprintf(2, "find: cannot stat %s\n", buf);
      continue;
    }
    //不要在“.”和“..”目录中递归
    if (st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) {
      find(buf, filename);
    } else if (strcmp(filename, p) == 0)
      printf("%s\n", buf);
  }

  close(fd);
}
```

### 5.3 实验中遇到的问题和解决办法

未遇到问题

### 5.4 实验心得

理解了文件查找的过程

## 6. xargs (moderate)
### 6.1 实验目的

编写一个简化版UNIX的xargs程序，从标准输入中按行读取，并且为每一行执行一个命令，将行作为参数提供给命令

### 6.2 实验步骤

- 使用有限状态自动机（FSA）解析输入，识别参数边界；
- 使用数组 x_argv 构造命令参数，添加输入内容；
- 每行结束即调用 fork + exec 执行命令；
- 测试输入多行、空格间隔、末尾多空格等边界情况是否正常处理

### 6.3 实验中遇到的问题和解决办法

空格、换行混合情况处理不一致：引入状态机判断字符类型和状态转换，使输入处理更加鲁棒；

### 6.4 实验心得

本次实验让我深入理解了命令参数的构造和 xargs 的行为原理，通过状态机设计有效提高了解析复杂输入的能力。同时熟悉了 fork、exec、wait 等系统调用的协作流程，是一次非常有收获的系统编程练习。