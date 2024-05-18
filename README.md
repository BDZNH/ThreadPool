ThreadPool
==========

A simple C++11 Thread Pool implementation.

Basic usage:
```c++
// create thread pool with 4 worker threads
ThreadPool pool(4);

// enqueue and store future
auto result = pool.enqueue([](int answer) { return answer; }, 42);

// get result from future
std::cout << result.get() << std::endl;

// post delay task and store future and the task will execute after 1000ms
auto result2 = pool.post(1000, [](int answer) { return answer; }, 43);

// get result from future
std::cout << result2.get() << std::endl;

```
