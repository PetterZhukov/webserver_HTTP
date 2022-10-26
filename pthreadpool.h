#ifndef _PTHREADPOOL_H_
#define _PTHREADPOOL_H_

#include <list>
#include "locker.h"
#include "questqueue.h"

// #define 控制
//#define show_create_pool 1
//#define show_pool_append 1

// 模板类 线程池
template <typename T>
class threadpool
{
public:
    threadpool(int poolsize = 8, int maxquest = 1000);
    ~threadpool();
    // 增加请求
    bool append(T *quest);

private:
    // 子线程调用的的执行函数
    static void *worker(void *arg);
    // 因为worker是静态的，因此增加一个真正的执行函数
    void run();

private:
    // 请求队列
    questqueue<T> m_questqueue;
    // 线程池大小
    int m_thread_poolsize;
    // 大小为m_thread_poolsize的 线程池
    pthread_t *m_threads;

    // 是否结束线程
    bool m_stoppool;
};

template <typename T>
threadpool<T>::threadpool(int poolsize, int maxquest) : 
    m_thread_poolsize(poolsize), m_stoppool(false),m_questqueue(maxquest)
{
    // check size
    if (maxquest <= 0 || poolsize <= 0)
        throw std::exception();

    m_threads = new pthread_t[m_thread_poolsize];

    // 初始化线程池的线程
    for (int i = 0; i < m_thread_poolsize; i++)
    {
        #ifdef  show_create_pool
            printf( "create the %dth thread\n", i+1);
        #endif
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
    for (int i = 0; i < m_thread_poolsize; i++)
    {
        if (pthread_detach(m_threads[i]) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
    printf("thread pool ready \n");
}

template <typename T>
threadpool<T>::~threadpool()
{
    m_stoppool = true;
    delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T *quest)
{
    return m_questqueue.push(quest);
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (!m_stoppool)
    {
        // 从请求队列中阻塞取出待处理元素
        T* quest=m_questqueue.pop();

        // 检查是否为空
        if (quest!=NULL)
            // 调用quest
            quest->process();
    }
}

#endif