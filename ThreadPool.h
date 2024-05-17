#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <chrono>

class ThreadPool {
public:
    ThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<
#if defined(__cpp_lib_is_invocable) && __cpp_lib_is_invocable >= 201703
        typename std::invoke_result<F&&, Args&&...>::type
#else
        typename std::result_of<F && (Args&&...)>::type
#endif
        >;
    template<class F, class... Args>
    auto post(uint64_t delayMs, F&& f, Args&&... args)
        -> std::future<
#if defined(__cpp_lib_is_invocable) && __cpp_lib_is_invocable >= 201703
        typename std::invoke_result<F&&, Args&&...>::type
#else
        typename std::result_of<F && (Args&&...)>::type
#endif
        >;
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    using Task = std::function<void()>;
    struct Event {
        uint64_t mWhenMs;
        Task mTask;
        Event() {
            mWhenMs = 0;
        }
        Event(uint64_t when, Task task) :mWhenMs(when), mTask(task) {

        }
    };
    std::vector< Event > tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    : stop(false)
{
    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back(
            [this]
            {
                Event task;
                while (!this->stop)
                {
                    task.mWhenMs = 0;
                    task.mTask = nullptr;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        if (this->tasks.empty())
                        {
                            this->condition.wait(lock,
                                [this] { return this->stop || !this->tasks.empty(); });
                            continue;
                        }

                        if (this->stop)
                            return;

                        uint64_t time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        if (tasks.begin()->mWhenMs <= time)
                        {
                            task = *(this->tasks.begin());
                            this->tasks.erase(this->tasks.begin());
                        }
                        else
                        {
                            this->condition.wait_for(lock, std::chrono::milliseconds(time - tasks.begin()->mWhenMs), [this] { return this->stop || !this->tasks.empty(); });
                            continue;
                        }

                        if (this->stop)
                            return;
                    }
                    if (task.mWhenMs > 0 && task.mTask != nullptr) {
                        task.mTask();
                    }
                }
            }
    );
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<
#if defined(__cpp_lib_is_invocable) && __cpp_lib_is_invocable >= 201703
    typename std::invoke_result<F&&, Args&&...>::type
#else
    typename std::result_of<F && (Args&&...)>::type
#endif
>
{
    return post(0, std::forward<F>(f), std::forward<Args>(args)...);
}

template<class F, class... Args>
auto ThreadPool::post(uint64_t delayMs, F&& f, Args&&... args)
->std::future<
#if defined(__cpp_lib_is_invocable) && __cpp_lib_is_invocable >= 201703
    typename std::invoke_result<F&&, Args&&...>::type
#else
    typename std::result_of<F && (Args&&...)>::type
#endif
>
{
#if defined(__cpp_lib_is_invocable) && __cpp_lib_is_invocable >= 201703
    using return_type = typename std::invoke_result<F&&, Args&&...>::type;
#else
    using return_type = typename std::result_of<F && (Args&&...)>::type;
#endif

    uint64_t whenms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    whenms += delayMs;
    auto task = std::make_shared< std::packaged_task<return_type()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        auto iter = tasks.begin();
        while (iter != tasks.end() && iter->mWhenMs <= whenms) {
            iter++;
        }
        tasks.insert(iter, { whenms, [task] {
            (*task)();
            } });
    }
    condition.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers)
        worker.join();
}

#endif
