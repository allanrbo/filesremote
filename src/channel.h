// Copyright 2020 Allan Riordan Boll

#ifndef SRC_CHANNEL_H_
#define SRC_CHANNEL_H_

#include <chrono>
#include <condition_variable>  // NOLINT
#include <list>
#include <mutex>  // NOLINT
#include <optional>

using std::chrono::milliseconds;
using std::condition_variable;
using std::list;
using std::mutex;
using std::nullopt;
using std::optional;
using std::unique_lock;

// Inspired by https://st.xorian.net/blog/2012/08/go-style-channel-in-c/ .
template<typename T>
class Channel {
private:
    list<T> queue;
    mutex m;
    condition_variable cv;

public:
    void Put(const T &i);

    // Blocks until available.
    T Get();

    optional<T> Get(milliseconds timeout);

    // Does not block.
    optional<T> TryGet();

    void Clear();
};

template<typename T>
void Channel<T>::Put(const T &i) {
    unique_lock<mutex> lock(m);
    queue.push_back(i);
    cv.notify_one();
}

// Blocks until available.
template<typename T>
T Channel<T>::Get() {
    unique_lock<mutex> lock(m);
    cv.wait(lock, [&]() {
        return !queue.empty();
    });
    T result = queue.front();
    queue.pop_front();
    return result;
}

// Blocks until available or timeout.
template<typename T>
optional<T> Channel<T>::Get(milliseconds timeout) {
    unique_lock<mutex> lock(m);
    bool r = cv.wait_for(lock, timeout, [&]() {
        return !queue.empty();
    });
    if (!r) {
        // Timed out.
        return nullopt;
    }
    T result = queue.front();
    queue.pop_front();
    return result;
}

// Does not block.
template<typename T>
optional<T> Channel<T>::TryGet() {
    unique_lock<mutex> lock(m);
    if (queue.empty()) {
        return nullopt;
    }
    T result = queue.front();
    queue.pop_front();
    return result;
}

template<typename T>
void Channel<T>::Clear() {
    while (this->TryGet()) {}
}

#endif  // SRC_CHANNEL_H_
