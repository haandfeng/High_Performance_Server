//filename:threadpool.h
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h> /*引用第14章介绍的线程同步机制的包装类*/
#include"../C14/locker.h" /*线程池类，将它定义为模板类是为了代码复用。模板参数T是任务类*/
template<typename T>
class threadpool
{
public:
    /*参数thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int thread_number=8,int max_requests=10000); ~threadpool();
    /*往请求队列中添加任务*/
    bool append(T*request);
private:
/*工作线程运行的函数，它不断从工作队列中取出任务并执行之，必须是静态的*/
    static void*worker(void*arg);
    void run();
private:
    int m_thread_number;/*线程池中的线程数*/
    int m_max_requests;/*请求队列中允许的最大请求数*/
    pthread_t*m_threads;/*描述线程池的数组，其大小为m_thread_number*/
    std::list<T*>m_workqueue;/*请求队列*/
    locker m_queuelocker;/*保护请求队列的互斥锁*/
    sem m_queuestat;/*是否有任务需要处理*/
    bool m_stop;/*是否结束线程*/
};

template<typename T>
threadpool<T>::threadpool(int thread_number,int max_requests):m_thread_number(thread_number),m_max_requests(max_requests),m_stop(false),m_threads(NULL)
{
    if((thread_number<=0)||(max_requests<=0))
    {
        throw std::exception();
    }
    m_threads=new pthread_t[m_thread_number];
    if(!m_threads)
    {
        throw std::exception();
    }
    /*创建thread_number个线程，并将它们都设置为脱离线程*/
    for(int i=0;i<thread_number;++i)
    {
        printf("create the%dth thread\n",i);
        /*int pthread_create(pthread_t *thread, const pthread_attr_t *attr,void *(*start_routine) (void *), void *arg);
        - `thread`: 一个指向 `pthread_t` 类型的指针，用于存储新创建线程的标识符。
        - `attr`: 一个指向 `pthread_attr_t` 类型的指针，用于设置新线程的属性。通常设置为 `NULL` 表示使用默认属性。
        - `start_routine`: 是一个函数指针，指向新线程所要执行的函数。这个函数必须返回 `void*` 类型，且接受一个 `void*` 类型的参数。
        - `arg`: 是传递给 `start_routine` 函数的参数。
        `pthread_create()` 函数成功时返回 `0`，表示线程创建成功；如果出现错误，返回非零值，可以通过 `errno` 来获取错误信息。
         这段代码是在创建一个新的线程。让我们逐步解释：
        - `pthread_create`: 这是 POSIX 线程库中用于创建新线程的函数。
        - `m_threads + i`: 这是线程数组中第 `i` 个元素的地址。`m_threads` 是线程数组的首地址，`i` 是线程的索引。通过 `m_threads + i` 可以获取到要创建线程的地址。
        - `NULL`: 这是一个指向线程属性的指针，通常设置为 `NULL` 表示使用默认属性。
        - `worker`: 这是一个函数指针，指向要在线程中执行的函数。在这里，它指向的是 `worker` 函数。
        - `this`: 这个参数是传递给 `worker` 函数的参数。在这里，它传递了当前对象的指针，因此 `worker` 函数可以访问当前对象的成员变量和方法。
        - `!= 0`: 这是一个条件判断，检查 `pthread_create` 函数的返回值是否为非零，如果不是零，表示线程创建失败。
        综上所述，这行代码的作用是创建一个新的线程，让它执行 `worker` 函数，并将当前对象的指针作为参数传递给 `worker` 函数。*/
        if(pthread_create(m_threads+i,NULL,worker,this)!=0)
        {
            delete[]m_threads;
            throw std::exception();
        }
        /*  int pthread_detach(pthread_t thread);
         * pthread_detach() 函数用于将线程标记为可分离的。这意味着当线程退出时，其资源会被自动释放，无需其他线程调用 pthread_join() 来等待该线程退出。
         * 成功时返回 0，失败时返回错误代码*/
        if(pthread_detach(m_threads[i]))
        {
            delete[]m_threads;
            throw std::exception();
        }
    }
}
template<typename T> threadpool<T>::~threadpool()
{
    delete[]m_threads;
    m_stop=true;
}
template<typename T> bool threadpool<T>::append(T*request)
{ /*操作工作队列时一定要加锁(互斥锁)，因为它被所有线程共享*/
    m_queuelocker.lock();
    if(m_workqueue.size()>m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
//    增加信号量
    m_queuestat.post();
    return true;
}
template<typename T> void*threadpool<T>::worker(void*arg)
{
    threadpool*pool=(threadpool*)arg;
    pool->run();
    return pool;
}
template<typename T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
//        减少信号量
        m_queuestat.wait();
//        互斥锁，对队列操作
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T*request=m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request)
        {
            continue;
        }
        request->process();
    }
}
#endif