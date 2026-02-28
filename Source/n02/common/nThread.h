/*
 * n02 - Open Kaillera Client
 * Thread abstraction using std::thread
 * Rewritten: replaced Win32 CreateThread/pthread with std::thread
 */
#pragma once

#ifndef TRACE

    #ifndef MSTR
    #define MSTR(X) #X
    #endif

    #ifdef N_DEBUG
        #define TRACE(CLASS_, FCN_, OBJ_)\
            printf("(%x)%s::%s\t\t" __FILE__ ":%i\n", OBJ_, MSTR(CLASS_), MSTR(FCN_),__LINE__);
    #else
        #define TRACE(X, Y, Z)
    #endif

#endif

#include "nSTL.h"

#include <thread>
#include <chrono>
#include <atomic>

// Maximum number of threads
#define NTHREAD_MAX 256

// Priority constants (informational only - std::thread doesn't support priorities)
#define NTHREAD_PRIORITY_CRITICAL   15
#define NTHREAD_PRIORITY_HIGH       2
#define NTHREAD_PRIORITY_NORMAL     0
#define NTHREAD_PRIORITY_LOW        -2
#define NTHREAD_PRIORITY_IDLE       -15


class nPThread {
protected:
    std::thread m_thread;
    std::atomic<bool> m_running{false};

public:
    virtual ~nPThread() {
        if (m_thread.joinable()) {
            m_thread.detach();
        }
    }

    virtual void run(void) {}

    int create() {
        try {
            m_running = true;
            m_thread = std::thread([this]() {
                try {
                    this->run();
                } catch (...) {
                    // Swallow exceptions in thread
                }
                m_running = false;
            });
            return -1; // Success (original API returns -1 on success)
        } catch (...) {
            m_running = false;
            return 0; // Failure
        }
    }

    int capture() {
        // Adopt current thread - no-op with std::thread
        // (used in original to track the calling thread)
        return 0;
    }

    int destroy() {
        // Can't safely kill a thread in C++
        // Just detach and let it finish
        if (m_thread.joinable()) {
            m_thread.detach();
        }
        m_running = false;
        return 1;
    }

    int prioritize(int priority) {
        // std::thread doesn't support priority changes
        (void)priority;
        return 0;
    }

    int status() {
        return m_running ? 1 : 0;
    }

    void yield() {
        std::this_thread::yield();
    }

    void sleep(int seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
    }

    bool isRunning() const {
        return m_running;
    }
};


#define nThread nPThread
