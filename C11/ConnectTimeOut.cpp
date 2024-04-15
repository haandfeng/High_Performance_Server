//
// Created by 谭演锋 on 2024/4/15.
//
//利用alarm函 数周期性地触发SIGALRM信号，该信号的信号处理函数利用管道通知 主循环执行定时器链表上的定时任务——关闭非活动的连接
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<assert.h>
#include<stdio.h>
#include<errno.h>
#include<fcntl.h>
#include<unistd.h>
#include<string.h>
#include <libgen.h>

/*超时连接函数*/
int timeout_connect(const char*ip,int port,int time) {
    int ret=0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family=AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port=htons(port);
    int sockfd=socket(PF_INET,SOCK_STREAM,0);
    assert(sockfd>=0);
    /*通过选项SO_RCVTIMEO和SO_SNDTIMEO所设置的超时时间的类型是timeval，这和select系统调用的超时参数类型相同*/
    struct timeval timeout;
    timeout.tv_sec=time;
    timeout.tv_usec=0;
    socklen_t len=sizeof(timeout);
    /*`setsockopt()` 函数用于设置套接字选项，其中包括套接字的各种属性和行为。在这里，`setsockopt()` 函数被用来设置套接字的发送超时选项。
    具体来说，这行代码中的参数含义如下：
    - `sockfd`：要设置选项的套接字描述符。
    - `SOL_SOCKET`：表示要设置的选项级别，这里表示设置的是套接字级别的选项。
    - `SO_SNDTIMEO`：表示要设置的选项名称，即发送超时选项。
    - `&timeout`：指向存放发送超时时间的结构体变量的指针，其中 `timeout` 是一个 `struct timeval` 结构体变量，用于设置发送超时的时间。
    - `len`：表示选项值的长度，即 `timeout` 结构体的大小。
    通过调用 `setsockopt()` 函数设置套接字的发送超时选项，可以控制在发送数据时的超时行为，从而避免由于网络等原因导致的长时间阻塞。*/
    ret=setsockopt(sockfd,SOL_SOCKET,SO_SNDTIMEO,&timeout,len);
    assert(ret!=-1);
    ret=connect(sockfd,(struct sockaddr*)&address,sizeof(address));
    if(ret==-1)
    {
        /*超时对应的错误号是EINPROGRESS。下面这个条件如果成立，我们就可以处理定时任
务了*/
        if(errno==EINPROGRESS)
        {
            printf("connecting timeout,process timeout logic\n"); return-1;
        }
        printf("error occur when connecting to server\n"); return-1;
    }
    return sockfd;
}
int main(int argc,char*argv[])
{if(argc<=2)
    {
        printf("usage:%s ip_address port_number\n",basename(argv[0])); return 1;
    }
    const char*ip=argv[1];
    int port=atoi(argv[2]);
    int sockfd=timeout_connect(ip,port,10);
    if(sockfd<0)
    {
        return 1;
    }
    return 0;
}