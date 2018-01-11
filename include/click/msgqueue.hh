// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_MSGQUEUE_HH
#define CLICK_MSGQUEUE_HH

#include <pthread.h>

#include <queue>

#include <click/string.hh>

CLICK_DECLS

struct Message
{
    String cmd;
    String arg;
    int id;

    Message() {
        cmd = ""; // not a real message
    }
};

class MsgQueue { public:

    MsgQueue();
    ~MsgQueue();

    bool empty();

    int size();

    void lock();

    void unlock();

    void wait();

    void wake();

    void add_message(Message msg);

    Message get_message();

  private:

    pthread_mutex_t _lock;

    pthread_cond_t _cond;

    std::queue<Message> _queue;
};

inline
MsgQueue::MsgQueue()
{
    pthread_mutex_init(&_lock, NULL);
    pthread_cond_init(&_cond, NULL);
}

inline
MsgQueue::~MsgQueue()
{
}

inline
bool MsgQueue::empty() {
    return size() == 0;
}

inline
int MsgQueue::size() {
    return _queue.size();
}

inline
void MsgQueue::lock() {
    pthread_mutex_lock(&_lock);
}

inline
void MsgQueue::unlock() {
    pthread_mutex_unlock(&_lock);
}

inline
void MsgQueue::wait() {
    pthread_cond_wait(&_cond, &_lock);
}

inline
void MsgQueue::wake() {
    pthread_cond_signal(&_cond);
}

inline
void MsgQueue::add_message(Message msg) {
    _queue.push(msg);
}

inline
Message MsgQueue::get_message() {
    if(_queue.empty())
        return Message();
    Message ret = _queue.front();
    _queue.pop();
    return ret;
}

CLICK_ENDDECLS
#endif
