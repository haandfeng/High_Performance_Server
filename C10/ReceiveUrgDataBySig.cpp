//
// Created by 谭演锋 on 2024/4/14.
//
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<errno.h>
#include<string.h>
#include<signal.h>
#include<fcntl.h>
#include <libgen.h>
#define BUF_SIZE 1024
static int connfd; /*SIGURG信号的处理函数*/
void sig_urg(int sig)
{
int save_errno=errno;
char buffer[BUF_SIZE];
memset(buffer,'\0',BUF_SIZE);
int ret=recv(connfd,buffer,BUF_SIZE-1,MSG_OOB);/*接收带外数据*/
printf("got%d bytes of oob data'%s'\n",ret,buffer);
errno=save_errno;
}
void addsig(int sig,void(*sig_handler)(int))
{
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=sig_handler;
    sa.sa_flags|=SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=-1);
}
int main(int argc,char*argv[])
{
    if(argc<=2)
    {
        printf("usage:%s ip_address port_number\n",basename(argv[0]));
        return 1;
    }
    const char*ip=argv[1];
    int port=atoi(argv[2]);
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family=AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port=htons(port);
    int sock=socket(PF_INET,SOCK_STREAM,0);
    assert(sock>=0);
    int ret=bind(sock,(struct sockaddr*)&address,sizeof(address));
    assert(ret!=-1);
    ret=listen(sock,5);
    assert(ret!=-1);
    struct sockaddr_in client;
    socklen_t client_addrlength=sizeof(client);
    connfd=accept(sock,(struct sockaddr*)&client,&client_addrlength);
    if(connfd<0)
    {
        printf("errno is:%d\n",errno);
    }
    else
    {
        /*在使用 `SIGURG` 信号之前，必须设置套接字的宿主进程或进程组。
        * 这是因为 `SIGURG` 信号通常用于指示带外数据到达套接字，而带外数据的到达对于应用程序来说可能是一种紧急情况，需要立即处理。
        * 但是，内核不知道应该向哪个进程发送 `SIGURG` 信号，因此需要通过 `fcntl()` 函数设置套接字的宿主进程或进程组，
        * 以便内核知道在发生紧急情况时将信号发送给哪个进程。*/
        addsig(SIGURG,sig_urg);
        /*使用SIGURG信号之前，我们必须设置socket的宿主进程或进程组
         * `fcntl(connfd, F_SETOWN, getpid())` 这行代码将当前进程的进程ID设置为套接字 `connfd` 的宿主进程ID，
         * 这样当发生带外数据到达时，内核就会将 `SIGURG` 信号发送给当前进程。*/
        fcntl(connfd,F_SETOWN,getpid());
        char buffer[BUF_SIZE];
        while(1)/*循环接收普通数据*/
        {
            memset(buffer,'\0',BUF_SIZE);
            ret=recv(connfd,buffer,BUF_SIZE-1,0);
            if(ret<=0)
            {
                break;
            }
            printf("got%d bytes of normal data'%s'\n",ret,buffer); }
        close(connfd);
    }
    close(sock);
    return 0;
}
