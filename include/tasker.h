#ifndef TASKER_H_
#define TASKER_H_

#include <algorithm>
#include <atomic>
#include <future>
#include <thread>
#include <vector>

#include "concurrent_queue.h"

template <typename T, std::size_t N = 0>
class Tasker {
public:
    template <typename Function>
    Tasker(Function function)
    {
        static_assert(std::is_invocable_v<Function, T>);

        taskers_.reserve(count_);
        for (std::size_t n = 0; n < count_; ++n)
            taskers_.emplace_back(std::async(std::launch::async, &Tasker::Run<Function>, this, function, n));
    }

    ~Tasker()
    {
        Stop();
    }

    void Stop()
    {
        for (auto& queue : queues_)
            queue.Stop();

        for (auto& tasker : taskers_) {
            if (tasker.valid())
                tasker.get();
        }

        queues_.clear();
        taskers_.clear();
    }

    template <typename... Args>
    void Post(Args&&... args)
    {
        auto index = index_.fetch_add(1, std::memory_order_relaxed);

        // Schedule task
        for (std::size_t n = 0; n < count_; ++n) {
            if (queues_[(index + n) % count_].TryEmplace(std::forward<Args>(args)...))
                return;
        }

        queues_[index % count_].Emplace(std::forward<Args>(args)...);
    }

    void Clear()
    {
        for (auto& queue : queues_)
            queue.Clear();
    }

private:
    template <typename Function>
    void Run(Function function, std::size_t n)
    {
        auto& queue = queues_[n];
        std::vector<std::size_t> indices(count_);

        std::generate(std::begin(indices), std::end(indices), [n, size = count_]() mutable { return n++ % size; });

        while (true) {
            std::optional<T> value;

            // Steal task
            for (const auto index : indices) {
                if (value = queues_[index].TryPop(); value)
                    break;
            }

            if (!value) {
                if (value = queue.Pop(); !value)
                    break;
            }

            function(*std::move(value));
        }
    }

    std::size_t count_ { N != 0 ? N : std::max(1u, std::thread::hardware_concurrency()) };
    std::atomic_size_t index_ { 0 };
    std::vector<ConcurrentQueue<T>> queues_ { count_ };
    std::vector<std::future<void>> taskers_;
};

template <typename T>
class Tasker<T, 1> {
public:
    template <typename Function>
    Tasker(Function function)
        : tasker_ { std::async(std::launch::async, &Tasker::Run<Function>, this, std::move(function)) }
    {
        static_assert(std::is_invocable_v<Function, T>);
    }

    ~Tasker()
    {
        Stop();
    }

    void Stop()
    {
        queue_.Stop();

        if (tasker_.valid())
            tasker_.get();
    }

    template <typename... Args>
    void Post(Args&&... args)
    {
        queue_.Emplace(std::forward<Args>(args)...);
    }

    void Clear()
    {
        queue_.Clear();
    }

private:
    template <typename Function>
    void Run(Function function)
    {
        while (auto value = queue_.Pop())
            function(*std::move(value));
    }

    ConcurrentQueue<T> queue_;
    std::future<void> tasker_;
};

template <typename Derived, typename T, std::size_t N = 0>
class TaskerBase {
public:
    TaskerBase()
    {
        taskers_.reserve(count_);
        for (std::size_t n = 0; n < count_; ++n)
            taskers_.emplace_back(std::async(std::launch::async, &TaskerBase::Run, this, n));
    }

    ~TaskerBase() = default;
    TaskerBase(const TaskerBase&) = delete;
    TaskerBase& operator=(const TaskerBase&) = delete;

    TaskerBase(TaskerBase&& other) noexcept
    {
        for (std::size_t n = 0; n < count_; ++n)
            queues_[n] = std::move(other.queues_[n]);

        other.Stop();

        taskers_.reserve(count_);
        for (std::size_t n = 0; n < count_; ++n)
            taskers_.emplace_back(std::async(std::launch::async, &TaskerBase::Run, this, n));
    }

    TaskerBase& operator=(TaskerBase&& other) noexcept
    {
        assert(this != &other);

        for (std::size_t n = 0; n < count_; ++n)
            queues_[n] = std::move(other.queues_[n]);

        other.Stop();

        if (taskers_.empty()) {
            taskers_.reserve(count_);
            for (std::size_t n = 0; n < count_; ++n)
                taskers_.emplace_back(std::async(std::launch::async, &TaskerBase::Run, this, n));
        }

        return *this;
    }

    void Stop()
    {
        for (auto& queue : queues_)
            queue.Stop();

        for (auto& tasker : taskers_) {
            if (tasker.valid())
                tasker.get();
        }
    }

    template <typename... Args>
    void Post(Args&&... args)
    {
        auto index = index_.fetch_add(1, std::memory_order_relaxed);

        // Schedule task
        for (std::size_t n = 0; n < count_; ++n) {
            if (queues_[(index + n) % count_].TryEmplace(std::forward<Args>(args)...))
                return;
        }

        queues_[index % count_].Emplace(std::forward<Args>(args)...);
    }

    void Clear()
    {
        for (auto& queue : queues_)
            queue.Clear();
    }

private:
    void Run(std::size_t n)
    {
        auto& queue = queues_[n];
        std::vector<std::size_t> indices(count_);

        std::generate(std::begin(indices), std::end(indices), [n, size = count_]() mutable { return n++ % size; });

        while (true) {
            std::optional<T> value;

            // Steal task
            for (const auto index : indices) {
                if (value = queues_[index].TryPop(); value)
                    break;
            }

            if (!value) {
                if (value = queue.Pop(); !value)
                    break;
            }

            static_cast<Derived*>(this)->Process(*std::move(value));
        }
    }

    std::size_t count_ { N != 0 ? N : std::max(1u, std::thread::hardware_concurrency()) };
    std::atomic_size_t index_ { 0 };
    std::vector<ConcurrentQueue<T>> queues_ { count_ };
    std::vector<std::future<void>> taskers_;
};

template <typename Derived, typename T>
class TaskerBase<Derived, T, 1> {
public:
    TaskerBase()
        : tasker_ { std::async(std::launch::async, &TaskerBase::Run, this) }
    {
    }

    ~TaskerBase() = default;
    TaskerBase(const TaskerBase&) = delete;
    TaskerBase& operator=(const TaskerBase&) = delete;

    TaskerBase(TaskerBase&& other) noexcept
    {
        queue_ = std::move(other.queue_);
        other.Stop();

        tasker_ = std::async(std::launch::async, &TaskerBase::Run, this);
    }

    TaskerBase& operator=(TaskerBase&& other) noexcept
    {
        assert(this != &other);

        queue_ = std::move(other.queue_);
        other.Stop();

        if (!tasker_.valid())
            tasker_ = std::async(std::launch::async, &TaskerBase::Run, this);

        return *this;
    }

    void Stop()
    {
        queue_.Stop();

        if (tasker_.valid())
            tasker_.get();
    }

    template <typename... Args>
    void Post(Args&&... args)
    {
        queue_.Emplace(std::forward<Args>(args)...);
    }

    void Clear()
    {
        queue_.Clear();
    }

private:
    void Run()
    {
        while (auto value = queue_.Pop())
            static_cast<Derived*>(this)->Process(*std::move(value));
    }

    ConcurrentQueue<T> queue_;
    std::future<void> tasker_;
};

#endif // TASKER_H_