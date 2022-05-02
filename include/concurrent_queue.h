#ifndef CONCURRENT_QUEUE_H_
#define CONCURRENT_QUEUE_H_

#include <cassert>
#include <condition_variable>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <type_traits>
#include <utility>

template <typename T>
class ConcurrentQueue {
public:
    ConcurrentQueue() = default;
    ~ConcurrentQueue() = default;
    ConcurrentQueue(const ConcurrentQueue&) = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

    ConcurrentQueue(ConcurrentQueue&& other) noexcept(std::is_nothrow_move_assignable_v<std::queue<T>>)
    {
        std::lock_guard lock { other.mutex_ };

        queue_ = std::move(other.queue_);
    }

    ConcurrentQueue& operator=(ConcurrentQueue&& other) noexcept(std::is_nothrow_move_assignable_v<std::queue<T>>)
    {
        assert(this != &other);

        {
            std::scoped_lock lock { mutex_, other.mutex_ };

            queue_ = std::move(other.queue_);
        }

        return *this;
    }

    bool Empty() const
    {
        std::shared_lock lock { mutex_ };

        return queue_.empty();
    }

    auto Size() const
    {
        std::shared_lock lock { mutex_ };

        return queue_.size();
    }

    std::optional<T> Front() const
    {
        std::optional<T> value;

        if (std::shared_lock lock { mutex_ }; !queue_.empty())
            value.emplace(queue_.front());

        return value;
    }

    std::optional<T> Back() const
    {
        std::optional<T> value;

        if (std::shared_lock lock { mutex_ }; !queue_.empty())
            value.emplace(queue_.back());

        return value;
    }

    void Stop()
    {
        {
            std::lock_guard lock { mutex_ };

            done_ = true;
        }

        cv_.notify_all();
    }

    void Clear()
    {
        std::lock_guard lock { mutex_ };

        queue_ = {};
    }

    void Push(const T& value)
    {
        Emplace(value);
    }

    void Push(T&& value)
    {
        Emplace(std::move(value));
    }

    template <typename... Args>
    void Emplace(Args&&... args)
    {
        {
            std::lock_guard lock { mutex_ };

            if (done_)
                return;

            queue_.emplace(std::forward<Args>(args)...);
        }

        cv_.notify_one();
    }

    std::optional<T> Pop()
    {
        std::optional<T> value;

        {
            std::unique_lock lock { mutex_ };
            cv_.wait(lock, [this] { return done_ || !queue_.empty(); });

            if (!queue_.empty()) {
                value.emplace(std::move(queue_.front()));
                queue_.pop();
            }
        }

        return value;
    }

    bool TryPush(const T& value)
    {
        return TryEmplace(value);
    }

    bool TryPush(T&& value)
    {
        return TryEmplace(std::move(value));
    }

    template <typename... Args>
    bool TryEmplace(Args&&... args)
    {
        {
            std::unique_lock lock { mutex_, std::try_to_lock };

            if (!lock)
                return false;
            else if (done_)
                return false;

            queue_.emplace(std::forward<Args>(args)...);
        }

        cv_.notify_one();

        return true;
    }

    std::optional<T> TryPop()
    {
        std::optional<T> value;

        if (std::unique_lock lock { mutex_, std::try_to_lock }; lock && !queue_.empty()) {
            value.emplace(std::move(queue_.front()));
            queue_.pop();
        }

        return value;
    }

private:
    bool done_ { false };
    std::queue<T> queue_;
    mutable std::shared_mutex mutex_;
    std::condition_variable_any cv_;
};

#endif // CONCURRENT_QUEUE_H_