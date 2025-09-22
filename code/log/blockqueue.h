#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include <deque>
#include <condition_variable>
#include <mutex>
#include <sys/time.h>
using namespace std;

template <typename T>
class BlockQueue
{
public:
    explicit BlockQueue(size_t maxsize = 1000);
    ~BlockQueue();
    bool empty();
    bool full();
    void push_back(const T &item);
    void push_front(const T &item);
    bool pop(T &item);              // 弹出的任务放入item
    bool pop(T &item, int timeout); // 等待时间
    void clear();
    T front();
    T back();
    size_t capacity();
    size_t size();

    void flush();
    void Close();

private:
    deque<T> deq_;                    // 底层数据结构
    mutex mtx_;                       // 锁
    bool isClose_;                    // 关闭标志
    size_t capacity_;                 // 容量
    condition_variable condConsumer_; // 消费者条件变量
    condition_variable condProducer_; // 生产者条件变量
};
// 初始化
template <typename T>
BlockQueue<T>::BlockQueue(size_t maxsize) : capacity_(maxsize)
{
    assert(maxsize > 0);
    isClose_ = false;
}
// 析构
template <typename T>
BlockQueue<T>::~BlockQueue()
{
    Close();
}
// 是否为空
template <typename T>
bool BlockQueue<T>::empty()
{
    unique_lock<mutex> locker(mtx_);
    return deq_.empty();
}
// 是否为满
template <typename T>
bool BlockQueue<T>::full()
{
    unique_lock<mutex> locker(mtx_);
    return deq_.size() >= capacity_;
}
// 添加元素到队尾
template <typename T>
void BlockQueue<T>::push_back(const T &item)
{
    unique_lock<mutex> locker(mtx_);
    while (deq_.size() >= capacity_)
    {                               // 队列满了
        condProducer_.wait(locker); // 等待生产者条件变量
    }
    deq_.push_back(item);       // 添加元素到队尾
    condConsumer_.notify_one(); // 通知消费者
}
// 添加元素到队头
template <typename T>
void BlockQueue<T>::push_front(const T &item)
{
    unique_lock<mutex> locker(mtx_);
    while (deq_.size() >= capacity_)
    {                               // 队列满了
        condProducer_.wait(locker); // 等待生产者条件变量
    }
    deq_.push_front(item);      // 添加元素到队头
    condConsumer_.notify_one(); // 通知消费者
}
// 弹出队头元素，放入item
template <typename T>
bool BlockQueue<T>::pop(T &item)
{
    unique_lock<mutex> locker(mtx_);
    while (deq_.empty() || isClose_)
    {                               // 队列空了
        condConsumer_.wait(locker); // 等待消费者条件变量
        if (isClose_)
        {
            return false;
        }
    }
    item = deq_.front();        // 获取队头元素
    deq_.pop_front();           // 弹出队头元素
    condProducer_.notify_one(); // 通知生产者
    return true;
}
// 带超时的弹出
template <typename T>
bool BlockQueue<T>::pop(T &item, int timeout)
{
    unique_lock<mutex> locker(mtx_);
    while (deq_.empty())
    { // 队列空了
        if (condConsumer_.wait_for(locker, chrono::seconds(timeout)) == cv_status::timeout)
        {                 // 等待超时
            return false; // 返回失败
        }
        if (isClose_)
        {
            return false;
        }
    }
    item = deq_.front();        // 获取队头元素
    deq_.pop_front();           // 弹出队头元素
    condProducer_.notify_one(); // 通知生产者
    return true;
}
// 清空队列
template <typename T>
void BlockQueue<T>::clear()
{
    unique_lock<mutex> locker(mtx_);
    deq_.clear();
}
// 获取队头元素
template <typename T>
T BlockQueue<T>::front()
{
    unique_lock<mutex> locker(mtx_);
    return deq_.front();
}
// 获取队尾元素
template <typename T>
T BlockQueue<T>::back()
{
    unique_lock<mutex> locker(mtx_);
    return deq_.back();
}
// 获取队列容量
template <typename T>
size_t BlockQueue<T>::capacity()
{
    unique_lock<mutex> locker(mtx_);
    return capacity_;
}
// 获取队列大小
template <typename T>
size_t BlockQueue<T>::size()
{
    unique_lock<mutex> locker(mtx_);
    return deq_.size(); 
}
// 通知所有等待线程
template <typename T>
void BlockQueue<T>::flush() {
    condConsumer_.notify_one();
}
// 关闭队列
template <typename T>
void BlockQueue<T>::Close()
{
    {
        unique_lock<mutex> locker(mtx_);
        deq_.clear();
        isClose_ = true;           // 设置关闭标志
    }
    condConsumer_.notify_all(); // 通知所有消费者
    condProducer_.notify_all(); // 通知所有生产者
}
#endif
