//
// Created by 谭演锋 on 2024/4/18.
//1.CGI定义： CGI(CommonGateway Interface)是HTTP服务器与你的或其它机器上的程序进行“交谈”的一种工具，其程序须运行在网络服务器上。
// .CGI功能： 绝大多数的CGI程序被用来解释处理来自表单的输入信息，并在服务器产生相应的处理，或将相应的信息反馈给浏览器。CGI程序使网页具有交互功能。
// 3.CGI运行环境： CGI程序在UNIX操
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<sys/epoll.h>
#include<signal.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include <libgen.h>
#include"processpool.h"/*引用上一节介绍的进程池*/
/*用于处理客户CGI请求的类，它可以作为processpool类的模板参数*/
class cgi_conn
{
public:
cgi_conn(){}
~cgi_conn(){}
/*初始化客户连接，清空读缓冲区*/
void init(int epollfd,int sockfd,const sockaddr_in&client_addr)
{
    m_epollfd=epollfd;
    m_sockfd=sockfd;
    m_address=client_addr;
    memset(m_buf,'\0',BUFFER_SIZE);
    m_read_idx=0;
}
void process()
    {
        int idx=0;
        int ret=-1;
/*循环读取和分析客户数据*/
        while(true)
        {
            idx=m_read_idx;
            ret=recv(m_sockfd,m_buf+idx,BUFFER_SIZE-1-idx,0); /*如果读操作发生错误,则关闭客户连接。但如果是暂时无数据可读，则退出循环*/
            if(ret<0)
            {
                if(errno!=EAGAIN)
                {
                    removefd(m_epollfd,m_sockfd);
                }
                break;
            }
            /*如果对方关闭连接，则服务器也关闭连接*/
            else if(ret==0)
            {
                removefd(m_epollfd,m_sockfd);
                break;
            }
            else
            {
                m_read_idx+=ret;
                printf("user content is:%s\n",m_buf);
                /*如果遇到字符“\r\n”，则开始处理客户请求*/
                for(;idx<m_read_idx;++idx)
                {
                    if((idx>=1)&&(m_buf[idx-1]=='\r')&&(m_buf[idx]=='\n'))
                    {
                        break;
                    }
                }
                /*如果没有遇到字符“\r\n”，则需要读取更多客户数据*/
                if(idx==m_read_idx)
                {
//                    再读
                    continue;
                }
                m_buf[idx-1]='\0';
                char*file_name=m_buf;
                /*判断客户要运行的CGI程序是否存在
                 * ccess() 函数是一个 POSIX 标准函数，用于检查对文件的访问权限。
                 * 在这里，access(file_name, F_OK) 检查给定的文件名 file_name 是否存在。
                 * 如果文件存在，access() 函数返回 0；如果文件不存在或者出现其他错误，返回值为 -1。*/
                if(access(file_name,F_OK)==-1)
                {
                    removefd(m_epollfd,m_sockfd);
                    break;
                }
                /*创建子进程来执行CGI程序*/
                ret=fork();
                if(ret==-1)
                {
                    removefd(m_epollfd,m_sockfd);
                    break;
                }
                else if(ret>0)
                {
                /*父进程只需关闭连接*/
                    removefd(m_epollfd,m_sockfd);
                    break;
                }
                else
                {
                    /*子进程将标准输出定向到m_sockfd，并执行CGI程序*/
//                    关闭标准输出文件描述符。
                    close(STDOUT_FILENO);
//                    复制套接字文件描述符 m_sockfd，并将其作为标准输出文件描述符的副本。把标准输出1指向m_sockfd，所有的输出会输出到m_sockfd上
                    dup(m_sockfd);
//                    执行指定路径的可执行文件 m_buf。execl 函数会替换当前进程的映像，因此原先进程的代码和数据都会被新程序替代。
//                    这里的第一个参数 m_buf 是要执行的程序的路径，第二个参数 m_buf 是程序的名称，第三个参数 0 是结束符，表示参数列表结束。
                    execl(m_buf,m_buf,0);
                    exit(0);
                }
            }
        }
    }
private:
/*读缓冲区的大小*/
    static const int BUFFER_SIZE=1024;
    static int m_epollfd;
    int m_sockfd;
    sockaddr_in m_address;
    char m_buf[BUFFER_SIZE]; /*标记读缓冲中已经读入的客户数据的最后一个字节的下一个位置*/
    int m_read_idx;
};
int cgi_conn::m_epollfd=-1;
/*主函数*/
int main(int argc,char*argv[])
{
    if(argc<=2)
    {
        printf("usage:%s ip_address port_number\n",basename(argv[0])); return 1;
    }
    const char*ip=argv[1];
    int port=atoi(argv[2]);
    int listenfd=socket(PF_INET,SOCK_STREAM,0); assert(listenfd>=0);
    int ret=0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family=AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port=htons(port);
    ret=bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    assert(ret!=-1);
    ret=listen(listenfd,5);
    assert(ret!=-1);
// 在子进程里调用users[sockfd].process();users就是cgi_conn
    processpool<cgi_conn>*pool=processpool<cgi_conn>::create(listenfd);
    if(pool)
    {
        pool->run();
        delete pool;
    }
    close(listenfd);/*正如前文提到的，main函数创建了文件描述符listenfd，那么就由它亲自关闭之*/
    return 0;
}