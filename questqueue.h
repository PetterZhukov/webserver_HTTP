#ifndef __QUEST_QUEUE_H_
#define __QUEST_QUEUE_H_

#include <list>
#include "locker.h"

// 模板类 请求队列
template <typename T>
class questqueue
{
private:
    // 请求队列
    std::list<T *> m_questqueue;
    // 请求队列的最大长度
    int m_max_queue;
    // 保护请求队列的互斥锁
    mutex m_queue_mutex;
    // 是否有任务需要处理,信号量
    sem m_queue_sem;

public:
    questqueue(int max_queue);
    ~questqueue();
    // 阻塞式取元素
    T *pop();
    // 阻塞式填入元素
    bool push(T *quest);
};

template <typename T>
questqueue<T>::questqueue(int max_queue) : m_max_queue(max_queue)
{
    if (max_queue <= 0)
        throw std::exception();
}

template <typename T>
questqueue<T>::~questqueue()
{
    for (auto it = m_questqueue.begin(); it != m_questqueue.end(); it++)
        delete *it;
}

template <typename T>
bool questqueue<T>::push(T *quest)
{
    // 操作请求队列加锁
    m_queue_mutex.lock(); // lock
    if (m_questqueue.size() >= m_max_queue)
    {
        return false;
    }

    // 添加quest，并且更新
    m_questqueue.push_back(quest);
    m_queue_sem.post();

    m_queue_mutex.unlock(); // unlock
    return true;
}

template <typename T>
T *questqueue<T>::pop()
{
    // 上锁
    m_queue_sem.wait();
    m_queue_mutex.lock();
    if (m_questqueue.empty())
    {
        m_queue_mutex.unlock();
        return NULL;
    }
    // 取出
    T *quest = m_questqueue.front();
    m_questqueue.pop_front();
    // 解锁
    m_queue_mutex.unlock();

    return quest;
}

#endif