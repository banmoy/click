// -*- c-basic-offset: 4; related-file-name: "../../lib/master.cc" -*-
#ifndef CLICK_MASTER_HH
#define CLICK_MASTER_HH
#include <click/router.hh>
#include <click/atomic.hh>
#if CLICK_USERLEVEL
#include <unordered_map>
#include <click/msgqueue.hh>
#include <click/hashmap.hh>
# include <signal.h>
#endif
#if CLICK_NS
# include <click/simclick.h>
#endif
CLICK_DECLS

#define CLICK_DEBUG_MASTER 0

class Master { public:

    Master(int nthreads);
    Master(int capacity, int nthreads, int cmdthreads);
    ~Master();

    void use();
    void unuse();

    void block_all();
    void unblock_all();

    void pause();
    inline void unpause();
    bool paused() const                         { return _master_paused > 0; }

    inline int nthreads() const;
    inline RouterThread *thread(int id) const;
    void wake_somebody();

#if CLICK_USERLEVEL
    int add_signal_handler(int signo, Router *router, String handler);
    int remove_signal_handler(int signo, Router *router, String handler);
    void process_signals(RouterThread *thread);
    static void signal_handler(int signo);      // not really public
#endif

    Router* get_control_router() const {
        return _control_router;
    }

    void set_control_router(Router* control_router) {
        _control_router = control_router;
    }

    void kill_router(Router*);

#if CLICK_NS
    void initialize_ns(simclick_node_t *simnode);
    simclick_node_t *simnode() const            { return _simnode; }
#endif

#if CLICK_DEBUG_MASTER || CLICK_DEBUG_SCHEDULING
    String info() const;
#endif

#if CLICK_USERLEVEL
    static volatile sig_atomic_t signals_pending;
#endif

  private:

    // THREADS
    RouterThread **_threads;
    int _capacity;
    int _nthreads;

    RouterThread **_cmd_threads;
    int _ncmdthreads;

    // ROUTERS
    Router *_routers;
    Router *_control_router;
    int _refcount;
    void register_router(Router*);
    void prepare_router(Router*);
    void run_router(Router*, bool foreground);
    void unregister_router(Router*);

#if CLICK_LINUXMODULE
    spinlock_t _master_lock;
    struct task_struct *_master_lock_task;
    int _master_lock_count;
#elif HAVE_MULTITHREAD
    Spinlock _master_lock;
#endif
    atomic_uint32_t _master_paused;
    inline void lock_master();
    inline void unlock_master();

    void request_stop();
    bool verify_stop(RouterThread* t);

#if CLICK_USERLEVEL
    // SIGNALS
    struct SignalInfo {
        int signo;
        Router *router;
        String handler;
        SignalInfo *next;
        SignalInfo(int signo_, Router *router_, const String &handler_)
            : signo(signo_), router(router_), handler(handler_), next() {
        }
        bool equals(int signo_, Router *router_, const String &handler_) const {
            return signo == signo_ && router == router_ && handler == handler_;
        }
    };
    SignalInfo *_siginfo;
    sigset_t _sig_dispatching;
    Spinlock _signal_lock;
#endif

#if CLICK_NS
    simclick_node_t *_simnode;
#endif

    Master(const Master&);
    Master& operator=(const Master&);

    friend class Task;
    friend class RouterThread;
    friend class Router;

private:
    int _msg_id;
    // -1 failed 0 processing 1 successful
    std::unordered_map<int, int> _msg_status;

public:
    pthread_rwlock_t _rw_lock;
    HashMap<String, Router*> _router_map;
    Vector<Router*> _unused_tasks;

    inline void lock_read() {
        pthread_rwlock_rdlock(&_rw_lock);
    }

    inline void lock_write() {
        pthread_rwlock_wrlock(&_rw_lock);
    }

    inline void unlock_rw() {
        pthread_rwlock_unlock(&_rw_lock);
    }

public:
    MsgQueue* _msg_queue;

#if (HAVE_MULTITHREAD)
  Vector<pthread_t> _pthreads;
#endif

    MsgQueue* get_msg_queue() const {
        return _msg_queue;
    }

    int get_msg_id() {
        return _msg_id++;
    }

    void set_msg_status(int id, int st) {
        _msg_status[id] = st;
    }

    int get_msg_status(int id) {
        if(_msg_status.count(id)) {
            return _msg_status[id];
        } else {
            // non exist
            return -2;
        }
    }

    Router* get_router(String rname) {
        return _router_map.find(rname, 0);
    }

    int add_thread();

    int run_nthreads() const;

    int max_cmd_thread() const;

    int min_cmd_thread() const;

    static int click_affinity_offset;

    static void *thread_driver(void *user_data);

    static void *thread_cmd_driver(void *user_data);

    static void do_set_affinity(pthread_t p, int cpu);
};

inline int
Master::min_cmd_thread() const {
    return _capacity - 1;
}

inline int
Master::max_cmd_thread() const {
    return _capacity -2 + _ncmdthreads;
}

inline int
Master::run_nthreads() const {
    return _nthreads - 2;
}

inline int
Master::nthreads() const
{
    return _nthreads - 1;
}

inline RouterThread*
Master::thread(int id) const
{
    // return the requested thread, or the quiescent thread if there's no such
    // thread
    if(id > _capacity-2)
        return _cmd_threads[id-(_capacity-2)-1];
    if (unsigned(id + 1) < unsigned(_nthreads))
        return _threads[id + 1];
    return _threads[0];
}

inline void
Master::wake_somebody()
{
    _threads[1]->wake();
}

#if CLICK_USERLEVEL
inline void
RouterThread::run_signals()
{
    if (Master::signals_pending)
        _master->process_signals(this);
}

inline int
TimerSet::next_timer_delay(bool more_tasks, Timestamp &t) const
{
# if CLICK_NS
    // The simulator should never block.
    (void) more_tasks, (void) t;
    return 0;
# else
    if (more_tasks || Master::signals_pending)
        return 0;
    t = timer_expiry_steady_adjusted();
    if (!t)
        return -1;              // block forever
    else if (unlikely(Timestamp::warp_jumping())) {
        Timestamp::warp_jump_steady(t);
        return 0;
    } else if ((t -= Timestamp::now_steady(), !t.is_negative())) {
        t = t.warp_real_delay();
        return 1;
    } else
        return 0;
# endif
}
#endif

inline void
Master::request_stop()
{
    for (RouterThread **t = _threads; t != _threads + _nthreads; ++t)
        (*t)->_stop_flag = true;
    // ensure that at least one thread is awake to handle the stop event
    wake_somebody();
}

inline void
Master::lock_master()
{
#if CLICK_LINUXMODULE
    if (current != _master_lock_task) {
        spin_lock(&_master_lock);
        _master_lock_task = current;
    } else
        _master_lock_count++;
#elif HAVE_MULTITHREAD
    _master_lock.acquire();
#endif
}

inline void
Master::unlock_master()
{
#if CLICK_LINUXMODULE
    assert(current == _master_lock_task);
    if (_master_lock_count == 0) {
        _master_lock_task = 0;
        spin_unlock(&_master_lock);
    } else
        _master_lock_count--;
#elif HAVE_MULTITHREAD
    _master_lock.release();
#endif
}

inline void
Master::unpause()
{
    _master_paused--;
}

inline Master *
Element::master() const
{
    return _router->master();
}

CLICK_ENDDECLS
#endif
