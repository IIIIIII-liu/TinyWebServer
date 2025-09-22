#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>

class ThreadPool
{
public:
    ThreadPool() = default;
    ThreadPool(ThreadPool &&) = default;
    ~ThreadPool()
    {
        if (pool_)
        {
            std::unique_lock<std::mutex> lock(pool_->mutex_);
            pool_->closed_ = true;
        }
        pool_->cond_.notify_all();
    }

    explicit ThreadPool(int threadCount = 8) : pool_(std::make_shared<POOL>())
    {
        pool_->closed_ = false;
        for (int i = 0; i < threadCount; ++i)
        {
            std::thread([pool = pool_]
                        {
                std::unique_lock<std::mutex> lock(pool->mutex_);
                while (true)
                {
                    if (!pool->tasks_.empty())
                    {
                        auto task = std::move(pool->tasks_.front());
                        pool->tasks_.pop();
                        lock.unlock();
                        task();
                        lock.lock();
                    }
                    else if (pool->closed_)
                    {
                        break;
                    }
                    else
                    {
                        pool->cond_.wait(lock);
                    }
                } })
                .detach();
        }
    }
    template <typename T>
    void AddTask(T &&task)
    {
        if (!pool_)
        {
            throw std::runtime_error("ThreadPool is not initialized");
        }
        {
            std::unique_lock<std::mutex> lock(pool_->mutex_);
            pool_->tasks_.emplace(std::forward<T>(task));
        }
        pool_->cond_.notify_one();
    }

private:
    struct POOL
    {
        std::mutex mutex_;
        std::condition_variable cond_;
        bool closed_;
        std::queue<std::function<void()>> tasks_;
    };
    std::shared_ptr<POOL> pool_;
};

#endif // THREADPOOL_H