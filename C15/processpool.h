//
// Created by 谭演锋 on 2024/4/18.
//
// 半同步半异步线程池
//filename:processpool.h
#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H
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
/*描述一个子进程的类，m_pid是目标子进程的PID，m_pipefd是父进程和子进程通信用的管道*/
class process
{
public:
    process():m_pid(-1){}
public:
    pid_t m_pid;
    int m_pipefd[2];
};
/*进程池类，将它定义为模板类是为了代码复用。其模板参数是处理逻辑任务的类*/
template<typename T>
class processpool
{
private:
    /*将构造函数定义为私有的，因此我们只能通过后面的create静态函数来创建单例processpool实例*/
    processpool(int listenfd,int process_number=8);
public:
    /*单体模式，以保证程序最多创建一个processpool实例，这是程序正确处理信号的必要条件*/
    static processpool<T>*create(int listenfd,int process_number=8) {
        if(!m_instance)
        {
            m_instance=new processpool<T>(listenfd,process_number);
        }
        return m_instance;
    }
    ~processpool()
    {
        delete[]m_sub_process;
    }
/*启动进程池*/
    void run();
private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();
private:
/*进程池允许的最大子进程数量*/
    static const int MAX_PROCESS_NUMBER=16; /*每个子进程最多能处理的客户数量*/
    static const int USER_PER_PROCESS=65536; /*epoll最多能处理的事件数*/
    static const int MAX_EVENT_NUMBER=10000;
/*进程池中的进程总数*/
    int m_process_number;
/*子进程在池中的序号，从0开始*/
    int m_idx;
/*每个进程都有一个epoll内核事件表，用m_epollfd标识，在setup_sig_pipe这个函数里面设置的*/
    int m_epollfd;
/*监听socket*/
    int m_listenfd;
/*子进程通过m_stop来决定是否停止运行*/
    int m_stop;
/*保存所有子进程的描述信息*/
    process*m_sub_process;
/*进程池静态实例*/
    static processpool<T>*m_instance;
};
//定义了一个静态指针成员变量m_instance，并初始化为NULL。这个指针通常用于指向processpool<T>类的唯一实例
template<typename T>
processpool<T>*processpool<T>::m_instance=NULL;

/*用于处理信号的管道，以实现统一事件源。后面称之为信号管道*/
static int sig_pipefd[2];

static int setnonblocking(int fd)
{
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

static void addfd(int epollfd,int fd)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}
/*从epollfd标识的epoll内核事件表中删除fd上的所有注册事件*/
static void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

static void sig_handler(int sig)
{
    int save_errno=errno;
    int msg=sig;
    send(sig_pipefd[1],(char*)&msg,1,0);
    errno=save_errno;
}

static void addsig(int sig,void(handler)(int),bool restart=true)
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
/*进程池构造函数。参数listenfd是监听socket，它必须在创建进程池之前被创建，否则子进程无法直接引用它。参数process_number指定进程池中子进程的数量*/
template<typename T>
processpool<T>::processpool(int listenfd,int process_number):m_listenfd(listenfd),m_process_number(process_number),m_idx(-1),m_stop(false)
{
    assert((process_number>0)&&(process_number<=MAX_PROCESS_NUMBER));
    m_sub_process=new process[process_number];
    assert(m_sub_process);
    /*创建process_number个子进程，并建立它们和父进程之间的管道*/
    for(int i=0;i<process_number;++i)
    {
/*  socketpair()函数用于创建一对无名的、相互连接的套接子。
    1. 这对套接字可以用于全双工通信，每一个套接字既可以读也可以写。例如，可以往sv[0]中写，从sv[1]中读；或者从sv[1]中写，从sv[0]中读；
    2. 如果往一个套接字(如sv[0])中写入后，再从该套接字读时会阻塞，只能在另一个套接字中(sv[1])上读成功；
    3. 读、写操作可以位于同一个进程，也可以分别位于不同的进程，如父子进程。如果是父子进程时，一般会功能分离，
    一个进程用来读，一个用来写。因为文件描述副sv[0]和sv[1]是进程共享的，所以读的进程要关闭写描述符, 反之，写的进程关闭读描述符。*/
        int ret=socketpair(PF_UNIX,SOCK_STREAM,0,m_sub_process[i].m_pipefd);
        assert(ret==0);
        m_sub_process[i].m_pid=fork();
        assert(m_sub_process[i].m_pid>=0);
        if(m_sub_process[i].m_pid>0)
        {
//            父进程，1 是读数据，0是写数据
            close(m_sub_process[i].m_pipefd[1]);
            continue;
        }
        else
        {
//            子进程，关了读取端，剩下写入端
            close(m_sub_process[i].m_pipefd[0]);
            m_idx=i;
//            break不会嵌套
            break;
        }
    }
}
/*统一事件源*/
template<typename T>
void processpool<T>::setup_sig_pipe()
{
/*创建epoll事件监听表和信号管道*/
    m_epollfd=epoll_create(5);
    assert(m_epollfd!=-1);
    int ret=socketpair(PF_UNIX,SOCK_STREAM,0,sig_pipefd);
    assert(ret!=-1);
    setnonblocking(sig_pipefd[1]);
    addfd(m_epollfd,sig_pipefd[0]);
/*设置信号处理函数
 * SIGCHLD这个信号通常表示子进程的状态改变，例如子进程终止或停止。父进程通常会使用 wait() 或 waitpid() 函数来等待并处理这些状态变化。
 * SIGTERM这个信号通常表示正常的终止请求，它是由 kill 命令或类似的工具发送给进程的。进程收到 SIGTERM 信号后，通常会优雅地关闭自身，并释放资源。
 * SIGINT 通常表示终端的中断信号，通常是用户在终端中按下 Ctrl+C 触发的。它用于中断正在运行的程序，并通知程序立即终止执行。*/
    addsig(SIGCHLD,sig_handler);
    addsig(SIGTERM,sig_handler);
    addsig(SIGINT,sig_handler);
/*这行代码的作用是忽略 SIGPIPE 信号。SIGPIPE 信号在进程尝试向已关闭的管道或套接字（socket）写入时触发。通常情况下，如果进程向已关闭的管道或套接字写入数据，
 * 操作系统会发送 SIGPIPE 信号给该进程，如果进程未处理该信号，则会导致进程异常终止。
 * 通过将 SIGPIPE 信号的处理方式设置为 SIG_IGN，即忽略该信号，
 * 可以防止进程因 SIGPIPE 而异常终止，从而提高程序的稳定性。*/
    addsig(SIGPIPE,SIG_IGN);
}
/*父进程中m_idx值为-1，子进程中m_idx值大于等于0，我们据此判断接下来要运行的是父进程代码还是子进程代码*/
template<typename T>
void processpool<T>::run()
{
    if(m_idx!=-1)
    {
        run_child();
        return;
    }
    run_parent();
}

template<typename T>
void processpool<T>::run_child()
{
//    在这里创建了m_pollfd
    setup_sig_pipe();
    /*每个子进程都通过其在进程池中的序号值m_idx找到与父进程通信的管道，只和父进程通信，收数据*/
    int pipefd=m_sub_process[m_idx].m_pipefd[1];
    /*子进程需要监听管道文件描述符pipefd，因为父进程将通过它来通知子进程accept新连接*/
//    每个进程都有自己的m_pollfd
    addfd(m_epollfd,pipefd);
    epoll_event events[MAX_EVENT_NUMBER];
    T*users=new T[USER_PER_PROCESS];
    assert(users);
    int number=0;
    int ret=-1;
    while(!m_stop)
    {
        number=epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number<0)&&(errno!=EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        for(int i=0;i<number;i++)
        {
            int sockfd=events[i].data.fd;
            if((sockfd==pipefd)&&(events[i].events&EPOLLIN))
            {
                int client=0;
                /*从父、子进程之间的管道读取数据，并将结果保存在变量client中。如果读取成功，则表示有新客户连接到来*/
                ret=recv(sockfd,(char*)&client,sizeof(client),0);
                if(((ret<0)&&(errno!=EAGAIN))||ret==0)
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength=sizeof(client_address);
//                  所有进程的m_listenfd都是一样的
                    int connfd=accept(m_listenfd,(struct sockaddr*)&client_address,&client_addrlength);
                    if(connfd<0)
                    {
                        printf("errno is:%d\n",errno);
                        continue;
                    }
//                    监听这个fd
                    addfd(m_epollfd,connfd);
                    /*模板类T必须实现init方法，以初始化一个客户连接。我们直接使用connfd来索引逻辑处理对象(T类型的对象)，以提高程序效率*/
                    users[connfd].init(m_epollfd,connfd,client_address);
                }
            }
            /*下面处理子进程接收到的信号*/
            else if((sockfd==sig_pipefd[0])&&(events[i].events&EPOLLIN)) {
                int sig;
                char signals[1024]; ret=recv(sig_pipefd[0],signals,sizeof(signals),0);
                if(ret<=0)
                {
                    continue;
                }
                else
                {
                    for(int i=0;i<ret;++i)
                    {
                        switch(signals[i])
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                /*waitpid(-1, &stat, WNOHANG)：这里的第一个参数 -1 表示等待任一子进程，
                                 * 第二个参数 &stat 是一个用于存储子进程状态的变量，最后一个参数 WNOHANG 表示非阻塞方式，即如果没有子进程结束，
                                 * waitpid 函数会立即返回而不会阻塞进程。*/
                                while((pid=waitpid(-1,&stat,WNOHANG))>0)
                                {
//                                    有结束就continue 没有就break
                                    continue;
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:{
                                m_stop=true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            /*如果是其他可读数据，那么必然是客户请求到来。调用逻辑处理对象的process方法处理之*/
            else if(events[i].events&EPOLLIN)
            {
                users[sockfd].process();
            }
            else
            {
                continue;
            }
        }
    }
    delete[]users;
    users=NULL;
    close(pipefd);
    //close(m_listenfd);
    /*我们将这句话注释掉，以提醒读者:应该由m_listenfd的创建者来关闭这个文件描述符(见后文)，
 * 即所谓的“对象(比如一个文件描述符，又或者 一段堆内存)由哪个函数创建，就应该由哪个函数销毁”*/
    close(m_epollfd);
}
template<typename T>
void processpool<T>::run_parent() {
    setup_sig_pipe(); /*父进程监听m_listenfd*/
    addfd(m_epollfd,m_listenfd);
    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter=0;
    int new_conn=1;
    int number=0;
    int ret=-1;
    while(!m_stop)
    {
        number=epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number<0)&&(errno!=EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        for(int i=0;i<number;i++) {
            int sockfd=events[i].data.fd;
            if(sockfd==m_listenfd)
            {
                /*如果有新连接到来，就采用Round Robin方式将其分配给一个子进程处理
                 * 这里的i不会影响外围循环for的i*/
                int i=sub_process_counter;
                do
                {
                    if(m_sub_process[i].m_pid!=-1)
                    {
                        break;
                    }
                    i=(i+1)%m_process_number;
                }
                while(i!=sub_process_counter);
//                都满了
                if(m_sub_process[i].m_pid==-1)
                {
                    m_stop=true;
                    break;
                }
                sub_process_counter=(i+1)%m_process_number;
                send(m_sub_process[i].m_pipefd[0], (char*)&new_conn,sizeof(new_conn),0);
                printf("send request to child%d\n",i);
            }
/*下面处理父进程接收到的信号*/
            else if((sockfd==sig_pipefd[0])&&(events[i].events&EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret=recv(sig_pipefd[0],signals,sizeof(signals),0);
                if(ret<=0)
                {
                    continue;
                }
                else
                {
                    for(int i=0;i<ret;++i)
                    {
                        switch(signals[i])
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while((pid=waitpid(-1,&stat,WNOHANG))>0)
                                {
                                    for(int i=0;i<m_process_number;++i)
                                    { /*如果进程池中第i个子进程退出了，则主进程关闭相应的通信管道，并设置相应的m_pid为-1，以标记该子进程已经退出*/
                                        if(m_sub_process[i].m_pid==pid)
                                        {
                                            printf("child%d join\n",i);
                                            close(m_sub_process[i].m_pipefd[0]);
                                            m_sub_process[i].m_pid=-1;
                                        }
                                    }
                                }
                                /*如果所有子进程都已经退出了，则父进程也退出*/
                                m_stop=true;
                                for(int i=0;i<m_process_number;++i)
                                {
                                    if(m_sub_process[i].m_pid!=-1)
                                    {
                                        m_stop=false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            { /*如果父进程接收到终止信号，那么就杀死(kill)所有子进程，并等待它们全部结束。当然，
                               * 通知子进程结束更好的方法是向父、子进程之间的通信管道发送特殊数据，读者不妨自己实现之*/
                                printf("kill all the clild now\n");
                                for(int i=0;i<m_process_number;++i) {
                                    int pid=m_sub_process[i].m_pid;
                                    if(pid!=-1)
                                    {
                                        kill(pid,SIGTERM);
                                    }
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                continue;
            }
        }
    }
    //close(m_listenfd);/*由创建者关闭这个文件描述符(见后文)*/
    close(m_epollfd);
}
#endif