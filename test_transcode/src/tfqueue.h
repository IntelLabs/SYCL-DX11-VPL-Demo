// Copyright 2024 Intel Corp.
// 
// Description: Thread-safe fixed size queue
//
// Source File: tfqueue.h
//
//
#pragma once

// For sync_queue implementation below
#include <mutex>
#include <condition_variable>
#include <chrono>

template<typename T>
class fs_queue
{
public:
    fs_queue(int max_size = 2):head(0), size(0) {
        data_queue.resize(max_size);
    }

    fs_queue(const fs_queue& other)
    {
        std::lock_guard<std::mutex> lock(other.mut);
        head = other.head;
        size = other.size;
        data_queue = other.data_queue;
    }

    void wait_and_push(const T& new_value)
    {
        std::unique_lock<std::mutex> lock(mut);
        tail_cond.wait(lock, [this] {return have_space(); });
        data_queue[(head + size) % data_queue.size()] = new_value;
        size+=1;
        data_cond.notify_one();
    }
    // push new value, if queue is full remove the oldest value and push
    // return true if discarded and old_value has data
    bool push_discard(const T& new_value, T& old_value)
    {
        bool discarded = false;
        std::lock_guard<std::mutex> lock(mut);
        if (have_space()) {
            data_queue[(head + size) % data_queue.size()] = new_value;
            size+=1;
        }
        else {
            old_value = data_queue[head];
            data_queue[head] = new_value;
            head = (head + 1) % data_queue.size();
            discarded = true;
        }
        data_cond.notify_one();
        return discarded;
    }
    void wait()
    {
        std::unique_lock<std::mutex> lock(mut);
        data_cond.wait(lock, [this] {return size > 0; });
    }

    void wait(int waitMs)
    {
        std::unique_lock<std::mutex> lock(mut);
        auto now = std::chrono::system_clock::now();
        data_cond.wait_until(lock, now + (waitMs * std::chrono::milliseconds(1)/*1ms*/), [this] {return size > 0; });
    }

    void wait_and_pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mut);
        data_cond.wait(lock, [this] {return size > 0; });
        value = data_queue[head];
        pop();
        tail_cond.notify_one();
    }


    bool try_pop(T& res)
    {
        std::lock_guard<std::mutex> lock(mut);
        if (size == 0) {
            return false;
        }
        res = data_queue[head];
        pop(); // unprotected pop
        tail_cond.notify_one();
        return true;
    }

	bool try_peek(T& res)
    {
        std::lock_guard<std::mutex> lock(mut);
        if (size==0) { 
			return false; 
		}
        res = data_queue[head];
		// do not pop
        return true;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mut);
        return size==0;
    }
    size_t count() const
    {
        std::lock_guard<std::mutex> lock(mut);
        return size;
    }
protected:
    void pop() {
        if (size > 0) {
            head = (head + 1) % data_queue.size();
            size-=1;
        }
    }
    bool have_space() const {
        return size < data_queue.size();
    }
private:
    mutable std::mutex mut;
    std::vector<T> data_queue;
    std::condition_variable data_cond;
    std::condition_variable tail_cond;
    volatile size_t head;
    volatile size_t size;
};

