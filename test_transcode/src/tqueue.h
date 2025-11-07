// Author: Binoy Marvar, UXR, CIL - 2013-2016
// 
// Description: Thread safe queue implementation to buffer frames
//
// Source File: tqueue.h
//
//
#pragma once

//For threadsafe_queue implementation below
#include <queue>
// For sync_queue implementation below
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

template<typename T>
class threadsafe_queue
{
public:
    threadsafe_queue() {}
    threadsafe_queue(const threadsafe_queue& other)
    {
        std::lock_guard<std::mutex> lock(other.mut);
        data_queue = other.data_queue;
    }

    void push(T new_value)
    {
        std::lock_guard<std::mutex> lock(mut);
        data_queue.push(new_value);
        data_cond.notify_one();
    }
    void wait()
    {
        std::unique_lock<std::mutex> lock(mut);
        data_cond.wait(lock, [this] {return !data_queue.empty(); });
    }

    void wait(int waitMs)
    {
        std::unique_lock<std::mutex> lock(mut);
        auto now = std::chrono::system_clock::now();
        data_cond.wait_until(lock, now + (waitMs * std::chrono::milliseconds(1)/*1ms*/), [this] {return !data_queue.empty(); });
    }

    void wait_and_pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mut);
        data_cond.wait(lock, [this]{return !data_queue.empty();});
        value = data_queue.front();
        data_queue.pop();
    }

    std::shared_ptr<T> wait_and_pop()
    {
        std::unique_lock<std::mutex> lock(mut);
        data_cond.wait(lock, [this]{return !data_queue.empty();});
        std::shared_ptr<T> result(std::make_shared<T>(data_queue.front()));
        data_queue.pop();
        return result;
    }

	bool try_pop(T& value)
    {
        std::lock_guard<std::mutex> lock(mut);
        if (data_queue.empty()) { return false; }
        value = data_queue.front();
        data_queue.pop();
        return true;
    }

    std::shared_ptr<T> try_pop()
    {
        std::lock_guard<std::mutex> lock(mut);
        if (data_queue.empty()) {
            return std::shared_ptr<T>();
        }
        std::shared_ptr<T> result(std::make_shared<T>(data_queue.front()));
        data_queue.pop();
        return result;
    }

	std::shared_ptr<T> try_peek()
    {
        std::lock_guard<std::mutex> lock(mut);
        if (data_queue.empty()) { 
			return std::shared_ptr<T>(); 
		}
        std::shared_ptr<T> result(std::make_shared<T>(data_queue.back()));
		// do not pop
        return result;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mut);
        return data_queue.empty();
    }
    size_t count() const
    {
        std::lock_guard<std::mutex> lock(mut);
        return data_queue.size();
    }
private:
    mutable std::mutex mut;
    std::queue<T> data_queue;
    std::condition_variable data_cond;
};

// naive synchronized queue implementation
template<typename T>
class sync_queue {
public:
    sync_queue(int max_size = -1) : max_size(max_size)
    {
        timeout_ms = 0;
    }

    T get()
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (q.empty()) {
            cond.wait(lock);
        }

        auto element = q.front();
        q.pop_front();
        lock.unlock();
        cond.notify_one();
        return element;
    }

    bool get(T& element, bool back = false)
    {
        std::unique_lock<std::mutex> lock(mtx);
        std::cv_status ret = std::cv_status::no_timeout;
        while (q.empty()) {
            if (timeout_ms == 0) {
                cond.wait(lock);
            }
            else {
                ret = cond.wait_for(lock,
                    std::chrono::milliseconds(timeout_ms));
                if (ret == std::cv_status::timeout) {
                    break;
                }
            }
        }

        if (ret == std::cv_status::no_timeout) {
            if (!back) {
                element = q.front();
                q.pop_front();
            }
            else {
                element = q.back();
                q.pop_back();
                // clean the entire q...
                if (q.size()) {
                    q.clear();
                }
            }
            lock.unlock();
            cond.notify_one();
            return true;
        }
        else {
            return false;
        }
    }

    bool put(T& element, bool front = false)
    {
        std::unique_lock<std::mutex> lock(mtx);
        std::cv_status ret = std::cv_status::no_timeout;
        while (max_size != -1 && q.size() >= max_size) {
            if (timeout_ms == 0) {
                cond.wait(lock);
            }
            else {
                ret = cond.wait_for(lock,
                    std::chrono::milliseconds(timeout_ms));
                if (ret == std::cv_status::timeout) {
                    break;
                }
            }
        }

        if (ret == std::cv_status::no_timeout) {
            if (front == false) {
                q.push_back(element);
            }
            else {
                q.push_front(element);
            }
            lock.unlock();
            cond.notify_one();
            return true;
        }
        else {
            return false;
        }
    }

    void update(T& element, bool front = false)
    {
        std::unique_lock<std::mutex> lock(mtx);
        int sz = (int)q.size();
        if (front == false) {
            if (sz) {
                q.pop_back();
            }
            q.push_back(element);
        }
        else {
            if (sz) {
                q.pop_front();
            }
            q.push_front(element);
        }
        lock.unlock();
        cond.notify_one();
    }

    int size()
    {
        std::unique_lock<std::mutex> lock(mtx);
        int sz = q.size();
        lock.unlock();
        cond.notify_one();
        return sz;
    }

    void set_timeout(int ms)
    {
        timeout_ms = ms;
    }

    void clear()
    {
        std::unique_lock<std::mutex> lock(mtx);
        q.clear();
        lock.unlock();
        cond.notify_one();
    }
private:
    int timeout_ms;
    int max_size;
    std::deque<T> q;
    std::mutex mtx;
    std::condition_variable cond;
};


class semaphore {
public:
    semaphore(int ms = 0)
    {
        // intial state is unlocked.
        count = 1;
        timeout_ms = (ms >= 0) ? ms : 0;
    }

    bool acquire()
    {
        std::unique_lock<std::mutex> lock(mtx);
        std::cv_status ret = std::cv_status::no_timeout;

        while (count <= 0) {
            if (timeout_ms == 0) {
                cond.wait(lock);
            }
            else {
                ret = cond.wait_for(lock,
                    std::chrono::milliseconds(timeout_ms));
                if (ret == std::cv_status::timeout) {
                    break;
                }
            }
        }

        if (ret == std::cv_status::no_timeout) {
            count--;
            return true;
        }
        else {
            return false;
        }
    }

    void release()
    {
        std::unique_lock<std::mutex> lock(mtx);
        count++;
        lock.unlock();
        cond.notify_one();
    }

    void set_timeout(int ms)
    {
        timeout_ms = ms;
    }

private:
    int timeout_ms;
    int count;
    std::mutex mtx;
    std::condition_variable cond;
};
