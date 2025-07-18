#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char const *argv[])
{
    #define RD 0 //pipe的read端
    #define WR 1 //pipe的write端

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