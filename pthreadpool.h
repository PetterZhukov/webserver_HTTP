#ifndef _PTHREADPOOL_H_
#define _PTHREADPOOL_H_

#include <list>
#include "locker.h"

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
    // 线程池大小
    int m_thread_poolsize;

    // 线程池，大小为m_thread_poolsize
    pthread_t *m_threads;

    // 请求队列
    std::list<T *> m_questqueue;

    // 请求队列的最大长度
    int m_max_queue;

    // 保护请求队列的互斥锁
    mutex m_queuemutex;

    // 是否有任务需要处理
    sem m_sem_queue;

    // 是否结束线程
    bool m_stoppool;
};

template <typename T>
threadpool<T>::threadpool(int poolsize, int maxquest) : 
    m_thread_poolsize(poolsize), m_max_queue(maxquest), m_stoppool(false)
{
    // check size
    if (maxquest <= 0 || poolsize <= 0)
        throw std::exception();

    m_threads = new pthread_t[m_thread_poolsize];

    // 初始化线程池的线程
    for (int i = 0; i < m_thread_poolsize; i++)
    {
        printf( "create the %dth thread\n", i);
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
}

template <typename T>
threadpool<T>::~threadpool()
{
    m_stoppool = true;
    delete[] m_threads;
    // 以防万一,delete questqueue中的每一个值
    for(auto it=m_questqueue.begin();it!=m_questqueue.end();it++)
        delete *it;
}

template <typename T>
bool threadpool<T>::append(T *quest)
{
    // 操作请求队列加锁
    m_queuemutex.lock();
    if (m_questqueue.size() >= m_max_queue)
    {
        return false;
    }

    // 添加quest，并且更新
    m_questqueue.push_back(quest);
    m_sem_queue.post();

    m_queuemutex.unlock();
    return true;
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
        // 上锁
        m_sem_queue.wait();
        m_queuemutex.lock();
        if (m_questqueue.empty())
        {
            m_queuemutex.unlock();
            continue;
        }
        // 取出
        T *quest = m_questqueue.front();
        m_questqueue.pop_front();
        // 解锁
        m_queuemutex.unlock();

        // 调用quest
        // 检查是否为空
        if (quest!=NULL)
            quest->process();
    }
}

#endif