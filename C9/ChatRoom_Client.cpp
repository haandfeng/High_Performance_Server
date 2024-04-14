//
// Created by 谭演锋 on 2024/4/13.
//
/*是一个预处理指令，用于告诉编译器启用 GNU 扩展特性。在 C 语言中，这个宏的定义通常用于激活 GNU/Linux 系统上的一些特定功能或者 GNU C 库中的特性。
在编译时，定义 _GNU_SOURCE 宏可以使编译器使用 GNU 扩展，以便在代码中使用 GNU 扩展提供的功能。这些功能可能包括一些非标准的库函数、数据类型或宏等。*/
#define _GNU_SOURCE 1
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<stdlib.h>
#include<poll.h>
#include<fcntl.h>
#include <libgen.h>
#define BUFFER_SIZE 64
int main(int argc,char*argv[]) {
    if(argc<=2)
    {
        printf("usage:%s ip_address port_number\n",basename(argv[0])); return 1;
    }
    const char*ip=argv[1];
    int port=atoi(argv[2]);
    struct sockaddr_in server_address;
    bzero(&server_address,sizeof(server_address));
    server_address.sin_family=AF_INET;
    inet_pton(AF_INET,ip,&server_address.sin_addr);
    server_address.sin_port=htons(port);
    int sockfd=socket(PF_INET,SOCK_STREAM,0);
    assert(sockfd>=0);
    if(connect(sockfd,(struct sockaddr*)&server_address,sizeof(server_address))<0)
    {
        printf("connection failed\n");
        close(sockfd);
        return 1;
    }
//POLLIN 表示对应的文件描述符上有数据可读。
//POLLRDHUP 表示对端连接关闭或者半关闭，即对方关闭了写操作或者关闭了写的一半。
//fds[0].fd = 0;：将第一个 pollfd 结构体中的文件描述符设置为标准输入文件描述符 0，表示标准输入。
    pollfd fds[2]; /*注册文件描述符0(标准输入)和文件描述符sockfd上的可读事件*/
    fds[0].fd=0;
    fds[0].events=POLLIN;
    fds[0].revents=0;
    fds[1].fd=sockfd;
    fds[1].events=POLLIN|POLLRDHUP;
    fds[1].revents=0;
    char read_buf[BUFFER_SIZE];
//    pipefd[0] 是管道的读取端，pipefd[1] 是管道的写入端。
    int pipefd[2];
    int ret=pipe(pipefd);
    assert(ret!=-1);
    while(1)
    {
        /*ret = poll(fds, 2, -1);：调用 poll() 函数等待两个文件描述符上的事件，阻塞直到有事件发生。
         * fds 是一个 pollfd 数组，2 表示数组的大小，-1 表示阻塞等待。ret 存储了 poll() 函数的返回值，表示发生事件的文件描述符的数量。*/
        ret=poll(fds,2,-1);
        if(ret<0){
            printf("poll failure\n");
            break;
        }
//       如果 fds[1] 上发生了对端连接关闭或者半关闭的事件。
        if(fds[1].revents&POLLRDHUP)
        {
            printf("server close the connection\n");
            break;
        }
//        如果 fds[1] 上有数据可读事件发生。
        else if(fds[1].revents&POLLIN)
        {
            memset(read_buf,'\0',BUFFER_SIZE);
            recv(fds[1].fd,read_buf,BUFFER_SIZE-1,0);
            printf("%s\n",read_buf);
        }
//        如果 fds[0] 上有数据可读事件发生（标准输入有输入）
        if(fds[0].revents&POLLIN)
        {
            /*使用splice将用户输入的数据直接写到sockfd上(零拷贝)
             * 从标准输入文件描述符 0（标准输入）读取数据，然后通过管道写入端文件描述符 pipefd[1] 中。
             * 32768 是一次传输的最大字节数。SPLICE_F_MORE 表示后续还有数据要写入，SPLICE_F_MOVE 表示移动数据而不复制数据。*/
            ret=splice(0,NULL,pipefd[1],NULL,32768,SPLICE_F_MORE|SPLICE_F_MOVE);
            /*从管道读取端文件描述符 pipefd[0] 中读取数据，然后通过套接字文件描述符 sockfd 写入。
             * 32768 是一次传输的最大字节数。SPLICE_F_MORE 表示后续还有数据要写入，SPLICE_F_MOVE 表示移动数据而不复制数据。*/
            ret=splice(pipefd[0],NULL,sockfd,NULL,32768,SPLICE_F_MORE|SPLICE_F_MOVE);
        }
    }
    close(sockfd);
    return 0;
}