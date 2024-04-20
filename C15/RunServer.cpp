//
// Created by 谭演锋 on 2024/4/18.
//
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<cassert>
#include<sys/epoll.h>
#include <libgen.h>
#include"../C14/locker.h"
#include"threadpool.h"
#include"http_conn.h"
#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
extern int addfd(int epollfd,int fd,bool one_shot);
extern int removefd(int epollfd,int fd);
void addsig(int sig,void(handler)(int),bool restart=true)
{
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=handler;
    if(restart)
    {
        sa.sa_flags|=SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=-1);
}
void show_error(int connfd,const char*info)
{
    printf("%s",info);
    send(connfd,info,strlen(info),0);
    close(connfd);
}
int main(int argc,char*argv[])
{
    if(argc<=2)
    {
        printf("usage:%s ip_address port_number\n",basename(argv[0])); return 1;
    }
    const char*ip=argv[1];
    int port=atoi(argv[2]);
/*忽略SIGPIPE信号
 * SIGPIPE信号通常在向已关闭的管道（或者socket）写入数据时发生，如果不忽略这个信号，在出现这种情况时程序会收到这个信号并退出，
 * 这样可能会影响程序的稳定性。*/
    addsig(SIGPIPE,SIG_IGN);
/*创建线程池*/
    threadpool<http_conn>*pool=NULL;
    try
    {
        pool=new threadpool<http_conn>;
    }
    /*在C++中，`catch(...)`是一种异常处理机制，它表示捕获所有类型的异常。当程序运行过程中发生了异常，如果没有与之匹配的具体异常处理块，
     * 就会进入到`catch(...)`块中执行相应的异常处理逻辑。通常情况下，`catch(...)`用于捕获未被程序员明确处理的异常，
     * 以确保程序在异常情况下不会崩溃，而是可以进行适当的处理或记录异常信息，提高程序的健壮性和稳定性。*/
    catch(...)
    {
        return 1;
    }
/*预先为每个可能的客户连接分配一个http_conn对象*/
    http_conn*users=new http_conn[MAX_FD];
    assert(users);
    int user_count=0;
    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    assert(listenfd>=0);
    /*这行代码定义了一个名为`tmp`的`struct linger`结构体变量，并初始化其成员值为1和0。
    `struct linger`结构体用于设置套接字关闭时的行为。它有两个成员变量：
    1. `l_onoff`：表示是否启用套接字延迟关闭功能。如果将其设置为非零值（通常为1），则启用延迟关闭；如果设置为0，则禁用延迟关闭。
     延迟关闭是一种网络编程中常用的技术，它可以让套接字在关闭之前等待一段时间。这个等待时间可以用来确保套接字发送缓冲区中的数据能够成功发送到对端，从而避免数据丢失。
    主要用途包括：
    1. **确保数据发送完整性：** 在关闭套接字之前，等待一段时间让发送缓冲区中的数据成功发送到对端，从而确保数据的完整性。
    2. **优雅关闭连接：** 对于TCP连接，延迟关闭可以让双方有足够的时间完成数据的交换和处理，然后再关闭连接，实现优雅的连接关闭过程。
    3. **避免发送RST包：** 如果在数据发送完毕前就立即关闭套接字，操作系统可能会发送一个RST（复位）包，表示连接异常终止。延迟关闭可以避免发送RST包，从而更加友好地关闭连接。
    总的来说，延迟关闭可以提高数据传输的可靠性和连接的稳定性，特别是在一些对数据完整性要求较高的场景下，如文件传输、视频流传输等。
    2. `l_linger`：表示延迟关闭的时间，单位是秒。当 `l_onoff` 设置为非零值时，`l_linger` 表示在关闭套接字之前等待的时间。如果 `l_linger` 设置为0，则表示立即关闭套接字，不等待。*/
    struct linger tmp={1,0};
    setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    int ret=0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family=AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port=htons(port);
    ret=bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    assert(ret>=0);
    ret=listen(listenfd,5);
    assert(ret>=0);
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);
    assert(epollfd!=-1);
    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd=epollfd;
    while(true)
    {
        int number=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number<0)&&(errno!=EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        for(int i=0;i<number;i++)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength=sizeof(client_address);
                int connfd=accept(listenfd,(struct sockaddr*)&client_address,&client_addrlength);
                if(connfd<0)
                {
                    printf("errno is:%d\n",errno);
                    continue;
                }
                if(http_conn::m_user_count>=MAX_FD)
                {
                    show_error(connfd,"Internal server busy");
                    continue;
                }
                /*初始化客户连接*/
                users[connfd].init(connfd,client_address);
            }
            else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))
            {
                /*如果有异常，直接关闭客户连接*/
                users[sockfd].close_conn();
            }
            else if(events[i].events&EPOLLIN)
            {
                /*根据读的结果，决定是将任务添加到线程池，还是关闭连接*/
                if(users[sockfd].read())
                {
//                    获取第sockfd对象的地址
                    pool->append(users+sockfd);
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events&EPOLLOUT)
            {
                /*根据写的结果，决定是否关闭连接*/
                if(!users[sockfd].write())
                {
                    users[sockfd].close_conn();
                }
            }
            else
            {}
        }
    }
    close(epollfd);
    close(listenfd);
    delete[]users;
    delete pool;
    return 0;
}