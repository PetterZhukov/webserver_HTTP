#ifndef _LOCKER_H_
#define _LOCKER_H_

#include <pthread.h>
#include <exception>
#include <semaphore.h>

// 互斥锁
class mutex
{
private:
    pthread_mutex_t m_mutex;

public:
    mutex(){
        if (pthread_mutex_init(&m_mutex, NULL) != 0){
            throw "初始化mutex错误";
        }
    }
    ~mutex(){
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
};

// 条件变量
class cond
{
private:
    pthread_cond_t m_cond;

public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw "初始化条件变量错误";
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *x)
    {
        return pthread_cond_wait(&m_cond, x) == 0;
    }
    // 唤醒一个或多个等待进程
    bool signal()
    {
        return pthread_cond_signal(&m_cond);
    }
    // 全部唤醒
    bool broadcast()
    {
        return pthread_cond_signal(&m_cond);
    }
};

// 信号量类
class sem
{
private:
    sem_t m_sem;

public:
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw "初始化信号量错误";
        }
    }
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw "初始化信号量错误";
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    bool post()
    {
        sem_post(&m_sem);
    }
    bool wait()
    {
        sem_wait(&m_sem);
    }
};

#endif
