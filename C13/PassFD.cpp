//
// Created by 谭演锋 on 2024/4/17.
//
#include<sys/socket.h>
#include<fcntl.h>
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<assert.h>
#include<string.h>
static const int CONTROL_LEN=CMSG_LEN(sizeof(int));
/*发送文件描述符，fd参数是用来传递信息的UNIX域socket，fd_to_send参数是待发送的文件描述符*/
void send_fd(int fd,int fd_to_send)
{
    /*iovec是用于readv()、writev()等系统调用的数据结构，用于支持读取或写入多个缓冲区的操作。它通常用于在单个系统调用中执行对多个非连续内存区域的读取或写入操作，提高了效率。
     * struct iovec {
     * void *iov_base;  缓冲区的起始地址
     * size_t iov_len;  缓冲区的长度 */
    struct iovec iov[1];
    /* msghdr是用于在套接字上发送和接收消息的数据结构，通常用于诸如sendmsg()和recvmsg()等系统调用。它包含了发送或接收消息的相关信息，例如消息的缓冲区、目标地址、辅助数据等。msghdr结构体定义如下：
     * struct msghdr {
    void         *msg_name;        目标地址
    socklen_t     msg_namelen;     目标地址的长度
    struct iovec *msg_iov;         指向数据缓冲区数组的指针
    int           msg_iovlen;      数据缓冲区数组的长度
    void         *msg_control;     指向辅助数据的指针
    socklen_t     msg_controllen;  辅助数据的长度
    int           msg_flags;       消息标志
    };*/

    struct msghdr msg;
    char buf[0];
    iov[0].iov_base=buf;
    iov[0].iov_len=1;
    msg.msg_name=NULL;
    msg.msg_namelen=0;
    msg.msg_iov=iov;
    msg.msg_iovlen=1;
    /*cmsghdr是用于处理辅助数据（ancillary data）的结构体，通常与msghdr结构体一起使用。
     * 辅助数据是在套接字上通过控制消息（control message）发送的额外信息，例如发送文件描述符等。
     * 在Linux系统中，通常使用cmsghdr结构体来处理这些控制消息。cmsghdr结构体定义如下：
     * struct cmsghdr {
    socklen_t   cmsg_len;   /* 控制消息的长度
    int         cmsg_level; /* 协议级别
    int         cmsg_type;  /* 控制消息类型
    /* 具体的辅助数据，通常是一个整数或者指针
    unsigned char  cmsg_data[];
    };
    */
    cmsghdr cm;
    cm.cmsg_len=CONTROL_LEN;
    cm.cmsg_level=SOL_SOCKET;
    cm.cmsg_type=SCM_RIGHTS;
    /*这行代码是将文件描述符 `fd_to_send` 存储到辅助数据中。具体来说，它通过 `CMSG_DATA(&cm)` 获取到辅助数据的起始地址，
     * 然后将 `fd_to_send` 的值存储到这个地址上，以便通过控制消息传递文件描述符。这是在使用UNIX域套接字等场景下，通过控制消息传递文件描述符的常见做法。*/
    *(int*)CMSG_DATA(&cm)=fd_to_send;
    msg.msg_control=&cm;/*设置辅助数据*/
    msg.msg_controllen=CONTROL_LEN;
    sendmsg(fd,&msg,0);
}
/*接收目标文件描述符*/
int recv_fd(int fd)
{
    struct iovec iov[1];
    struct msghdr msg;
    char buf[0];
    iov[0].iov_base=buf;
    iov[0].iov_len=1;
    msg.msg_name=NULL;
    msg.msg_namelen=0;
    msg.msg_iov=iov;
    msg.msg_iovlen=1;
    cmsghdr cm;
    msg.msg_control=&cm;
    msg.msg_controllen=CONTROL_LEN;
    recvmsg(fd,&msg,0);
    int fd_to_read=*(int*)CMSG_DATA(&cm);
    return fd_to_read;
}
int main()
{
    int pipefd[2];
    int fd_to_pass=0; /*创建父、子进程间的管道，文件描述符pipefd[0]和pipefd[1]都是UNIX域socket*/
    int ret=socketpair(PF_UNIX,SOCK_DGRAM,0,pipefd);
    assert(ret!=-1);
    pid_t pid=fork();
    assert(pid>=0);
    if(pid==0)
    {
        close(pipefd[0]);
        fd_to_pass=open("test.txt",O_RDWR,0666);
        /*子进程通过管道将文件描述符发送到父进程。如果文件test.txt打开失败，则子进程将标准输入文件描述符发送到父进程*/
        send_fd(pipefd[1],(fd_to_pass>0)?fd_to_pass:0); close(fd_to_pass);
        exit(0);
    }
    close(pipefd[1]); fd_to_pass=recv_fd(pipefd[0]);
    /*父进程从管道接收目标文件描述符*/
    char buf[1024];
    memset(buf,'\0',1024); read(fd_to_pass,buf,1024);
    /*读目标文件描述符，以验证其有效性*/
    printf("I got fd%d and data%s\n",fd_to_pass,buf);
    close(fd_to_pass);
}