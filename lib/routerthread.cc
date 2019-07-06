// -*- c-basic-offset: 4; related-file-name: "../include/click/routerthread.hh" -*-
/*
 * routerthread.{cc,hh} -- Click threads
 * Eddie Kohler, Benjie Chen, Petros Zerfos
 *
 * Copyright (c) 2000-2001 Massachusetts Institute of Technology
 * Copyright (c) 2001-2002 International Computer Science Institute
 * Copyright (c) 2004-2007 Regents of the University of California
 * Copyright (c) 2008-2010 Meraki, Inc.
 * Copyright (c) 2000-2016 Eddie Kohler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/router.hh>
#include <click/routerthread.hh>
#include <click/master.hh>
#include <click/driver.hh>
#include <click/error.hh>
#include <click/args.hh>
#if CLICK_LINUXMODULE
# include <click/cxxprotect.h>
CLICK_CXX_PROTECT
# include <linux/sched.h>
CLICK_CXX_UNPROTECT
# include <click/cxxunprotect.h>
#endif
#if CLICK_BSDMODULE
# include <click/cxxprotect.h>
CLICK_CXX_PROTECT
# include <sys/kthread.h>
CLICK_CXX_UNPROTECT
# include <click/cxxunprotect.h>
#elif CLICK_USERLEVEL
# include <click/msgqueue.hh>
# include <click/routerinfo.hh>
# include <fcntl.h>
# include <iostream>
# include <string>
# include <sstream>
# include <cmath>
# include <cstdlib> 
# include <ctime> 
#endif
CLICK_DECLS

#define DEBUG_RT_SCHED          0

#define PROFILE_ELEMENT         20

#if HAVE_ADAPTIVE_SCHEDULER
# define DRIVER_TOTAL_TICKETS   128     /* # tickets shared between clients */
# define DRIVER_GLOBAL_STRIDE   (Task::STRIDE1 / DRIVER_TOTAL_TICKETS)
# define DRIVER_QUANTUM         8       /* microseconds per stride */
# define DRIVER_RESTRIDE_INTERVAL 80    /* microseconds between restrides */
#endif

#if CLICK_LINUXMODULE
static unsigned long greedy_schedule_jiffies;
#endif

/** @file routerthread.hh
 * @brief The RouterThread class implementing the Click driver loop.
 */

/** @class RouterThread
 * @brief A set of Tasks scheduled on the same CPU.
 */

RouterThread::RouterThread(Master *master, int id)
    : _stop_flag(false), _master(master), _id(id), _driver_entered(false)
{
    _pending_head.x = 0;
    _pending_tail = &_pending_head;

#if !HAVE_TASK_HEAP
    _task_link._prev = _task_link._next = &_task_link;
#endif
#if CLICK_LINUXMODULE
    _linux_task = 0;
#elif CLICK_USERLEVEL && HAVE_MULTITHREAD
    _running_processor = click_invalid_processor();
#endif

    _task_blocker = 0;
    _task_blocker_waiting = 0;
#if HAVE_ADAPTIVE_SCHEDULER
    _max_click_share = 80 * Task::MAX_UTILIZATION / 100;
    _min_click_share = Task::MAX_UTILIZATION / 200;
    _cur_click_share = 0;       // because we aren't yet running
#endif

#if CLICK_NS
    _tasks_per_iter = 256;
#else
#ifdef BSD_NETISRSCHED
    // Must be set low for Luigi's feedback scheduler to work properly
    _tasks_per_iter = 8;
#else
    _tasks_per_iter = 128;
#endif
#endif

    _iters_per_os = 2;          // userlevel: iterations per select()
                                // kernel: iterations per OS schedule()

#if CLICK_LINUXMODULE || CLICK_BSDMODULE
    _greedy = false;
#endif
#if CLICK_LINUXMODULE
    greedy_schedule_jiffies = jiffies;
#endif

#if CLICK_NS
    _ns_scheduled = _ns_last_active = Timestamp(-1, 0);
    _ns_active_iter = 0;
#endif

#if CLICK_DEBUG_SCHEDULING
    _thread_state = S_BLOCKED;
    _driver_epoch = 0;
    _driver_task_epoch = 0;
    _task_epoch_first = 0;
# if CLICK_DEBUG_SCHEDULING > 1
    for (int s = 0; s < NSTATES; ++s)
        _thread_state_count[s] = 0;
# endif
#endif

    static_assert(THREAD_QUIESCENT == (int) ThreadSched::THREAD_QUIESCENT
                  && THREAD_UNKNOWN == (int) ThreadSched::THREAD_UNKNOWN,
                  "Thread constants screwup.");

    _task_num = 0;
}

RouterThread::~RouterThread()
{
    assert(!active());
}

void
RouterThread::driver_lock_tasks()
{
    set_thread_state(S_LOCKTASKS);

    // If other people are waiting for the task lock, give them a chance to
    // catch it before we claim it.
#if CLICK_LINUXMODULE
    for (int i = 0; _task_blocker_waiting > 0 && i < 10; i++)
        schedule();
#elif HAVE_MULTITHREAD && CLICK_USERLEVEL
    for (int i = 0; _task_blocker_waiting > 0 && i < 10; i++) {
        struct timeval waiter = { 0, 1 };
        select(0, 0, 0, 0, &waiter);
    }
#endif

    while (_task_blocker.compare_swap(0, (uint32_t) -1) != 0) {
#if CLICK_LINUXMODULE
        schedule();
#endif
    }
}

void
RouterThread::scheduled_tasks(Router *router, Vector<Task *> &x)
{
    lock_tasks();
    for (Task *t = task_begin(); t != task_end(); t = task_next(t))
        if (t->router() == router)
            x.push_back(t);
    unlock_tasks();
}


/******************************/
/* Adaptive scheduler         */
/******************************/

#if HAVE_ADAPTIVE_SCHEDULER

void
RouterThread::set_cpu_share(unsigned min_frac, unsigned max_frac)
{
    if (min_frac == 0)
        min_frac = 1;
    if (min_frac > Task::MAX_UTILIZATION - 1)
        min_frac = Task::MAX_UTILIZATION - 1;
    if (max_frac > Task::MAX_UTILIZATION - 1)
        max_frac = Task::MAX_UTILIZATION - 1;
    if (max_frac < min_frac)
        max_frac = min_frac;
    _min_click_share = min_frac;
    _max_click_share = max_frac;
}

void
RouterThread::client_set_tickets(int client, int new_tickets)
{
    Client &c = _clients[client];

    // pin 'tickets' in a reasonable range
    if (new_tickets < 1)
        new_tickets = 1;
    else if (new_tickets > Task::MAX_TICKETS)
        new_tickets = Task::MAX_TICKETS;
    unsigned new_stride = Task::STRIDE1 / new_tickets;
    assert(new_stride < Task::MAX_STRIDE);

    // calculate new pass, based possibly on old pass
    // start with a full stride on initialization (c.tickets == 0)
    if (c.tickets == 0)
        c.pass = _global_pass + new_stride;
    else {
        int delta = (c.pass - _global_pass) * new_stride / c.stride;
        c.pass = _global_pass + delta;
    }

    c.tickets = new_tickets;
    c.stride = new_stride;
}

inline void
RouterThread::client_update_pass(int client, const Timestamp &t_before)
{
    Client &c = _clients[client];
    Timestamp t_now = Timestamp::now();
    Timestamp::value_type elapsed = (t_now - t_before).usecval();
    if (elapsed > 0)
        c.pass += (c.stride * elapsed) / DRIVER_QUANTUM;
    else
        c.pass += c.stride;

    // check_restride
    elapsed = (t_now - _adaptive_restride_timestamp).usecval();
    if (elapsed > DRIVER_RESTRIDE_INTERVAL || elapsed < 0) {
        // mark new measurement period
        _adaptive_restride_timestamp = t_now;

        // reset passes every 10 intervals, or when time moves backwards
        if (++_adaptive_restride_iter == 10 || elapsed < 0) {
            _global_pass = _clients[C_CLICK].tickets = _clients[C_KERNEL].tickets = 0;
            _adaptive_restride_iter = 0;
        } else
            _global_pass += (DRIVER_GLOBAL_STRIDE * elapsed) / DRIVER_QUANTUM;

        // find out the maximum amount of work any task performed
        int click_utilization = 0;
        Task *end = task_end();
        for (Task *t = task_begin(); t != end; t = task_next(t)) {
            int u = t->utilization();
            t->clear_runs();
            if (u > click_utilization)
                click_utilization = u;
        }

        // constrain to bounds
        if (click_utilization < _min_click_share)
            click_utilization = _min_click_share;
        if (click_utilization > _max_click_share)
            click_utilization = _max_click_share;

        // set tickets
        int click_tix = (DRIVER_TOTAL_TICKETS * click_utilization) / Task::MAX_UTILIZATION;
        if (click_tix < 1)
            click_tix = 1;
        client_set_tickets(C_CLICK, click_tix);
        client_set_tickets(C_KERNEL, DRIVER_TOTAL_TICKETS - _clients[C_CLICK].tickets);
        _cur_click_share = click_utilization;
    }
}

#endif

/******************************/
/* Debugging                  */
/******************************/

#if CLICK_DEBUG_SCHEDULING
Timestamp
RouterThread::task_epoch_time(uint32_t epoch) const
{
    if (epoch >= _task_epoch_first && epoch <= _driver_task_epoch)
        return _task_epoch_time[epoch - _task_epoch_first];
    else if (epoch > _driver_task_epoch - TASK_EPOCH_BUFSIZ && epoch <= _task_epoch_first - 1)
        // "-1" makes this code work even if _task_epoch overflows
        return _task_epoch_time[epoch - (_task_epoch_first - TASK_EPOCH_BUFSIZ)];
    else
        return Timestamp();
}
#endif


/******************************/
/* The driver loop            */
/******************************/

#if HAVE_TASK_HEAP
#define PASS_GE(a, b)   ((int)(a - b) >= 0)

void
RouterThread::task_reheapify_from(int pos, Task* t)
{
    // MUST be called with task lock held
    task_heap_element *tbegin = _task_heap.begin();
    task_heap_element *tend = _task_heap.end();
    int npos;

    while (pos > 0
           && (npos = (pos-1) >> 1, PASS_GT(tbegin[npos].pass, t->_pass))) {
        tbegin[pos] = tbegin[npos];
        tbegin[npos].t->_schedpos = pos;
        pos = npos;
    }

    while (1) {
        Task *smallest = t;
        task_heap_element *tp = tbegin + 2*pos + 1;
        if (tp < tend && PASS_GE(smallest->_pass, tp[0].pass))
            smallest = tp[0].t;
        if (tp + 1 < tend && PASS_GE(smallest->_pass, tp[1].pass))
            smallest = tp[1].t, ++tp;

        smallest->_schedpos = pos;
        tbegin[pos].t = smallest;
        tbegin[pos].pass = smallest->_pass;

        if (smallest == t)
            return;

        pos = tp - tbegin;
    }
}
#endif

/* Run at most 'ntasks' tasks. */
inline void
RouterThread::run_tasks(int ntasks)
{
    set_thread_state(S_RUNTASK);
#if CLICK_DEBUG_SCHEDULING
    _driver_task_epoch++;
    _task_epoch_time[_driver_task_epoch % TASK_EPOCH_BUFSIZ].assign_now();
    if ((_driver_task_epoch % TASK_EPOCH_BUFSIZ) == 0)
        _task_epoch_first = _driver_task_epoch;
#endif
#if HAVE_ADAPTIVE_SCHEDULER
    Timestamp t_before = Timestamp::now();
#endif
#if CLICK_BSDMODULE && !BSD_NETISRSCHED
    int bsd_spl = splimp();
#endif

    // never run more than 32768 tasks
    if (ntasks > 32768)
        ntasks = 32768;

#if HAVE_MULTITHREAD
    // cycle counter for adaptive scheduling among processors
    click_cycles_t cycles = 0;
#endif

    Task::Status want_status;
    want_status.home_thread_id = thread_id();
    want_status.is_scheduled = true;
    want_status.is_strong_unscheduled = false;

    Task *t;
#if HAVE_MULTITHREAD
    int runs;
#endif
    bool work_done;

    for (; ntasks >= 0; --ntasks) {
        t = task_begin();
        if (t == task_end() || _stop_flag)
            break;
        assert(t->_thread == this);

        if (unlikely(t->_status.status != want_status.status)) {
            if (t->_status.home_thread_id != want_status.home_thread_id)
                t->add_pending(false);
            t->remove_from_scheduled_list();
            continue;
        }

#if HAVE_MULTITHREAD
        runs = t->cycle_runs();
        if (runs > PROFILE_ELEMENT)
            cycles = click_get_cycles();
#endif

        t->_status.is_scheduled = false;
        work_done = t->fire();

#if HAVE_MULTITHREAD
        if (runs > PROFILE_ELEMENT) {
            unsigned delta = click_get_cycles() - cycles;
            t->update_cycles(delta/32 + (t->cycles()*31)/32);
        }
#endif

        // fix task list
        if (t->scheduled()) {
            // adjust position in scheduled list
#if HAVE_STRIDE_SCHED
            t->_pass += t->_stride;
#endif

            // If the task didn't do any work, don't run it next.  This might
            // require delaying its pass, or exiting the scheduling loop
            // entirely.
            if (!work_done) {
#if HAVE_STRIDE_SCHED && HAVE_TASK_HEAP
                if (_task_heap.size() < 2)
                    break;
#else
                if (t->_next == &_task_link)
                    break;
#endif
#if HAVE_STRIDE_SCHED
# if HAVE_TASK_HEAP
                unsigned p1 = _task_heap.unchecked_at(1).pass;
                if (_task_heap.size() > 2 && PASS_GT(p1, _task_heap.unchecked_at(2).pass))
                    p1 = _task_heap.unchecked_at(2).pass;
# else
                unsigned p1 = t->_next->_pass;
# endif
                if (PASS_GT(p1, t->_pass))
                    t->_pass = p1;
#endif
            }

#if HAVE_STRIDE_SCHED && HAVE_TASK_HEAP
            task_reheapify_from(0, t);
#else
# if HAVE_STRIDE_SCHED
            TaskLink *n = t->_next;
            while (n != &_task_link && !PASS_GT(n->_pass, t->_pass))
                n = n->_next;
# else
            TaskLink *n = &_task_link;
# endif
            if (t->_next != n) {
                t->_next->_prev = t->_prev;
                t->_prev->_next = t->_next;
                t->_next = n;
                t->_prev = n->_prev;
                n->_prev->_next = t;
                n->_prev = t;
            }
#endif
        } else
            t->remove_from_scheduled_list();
    }

#if CLICK_BSDMODULE && !BSD_NETISRSCHED
    splx(bsd_spl);
#endif
#if HAVE_ADAPTIVE_SCHEDULER
    client_update_pass(C_CLICK, t_before);
#endif
}

inline void
RouterThread::run_os()
{
#if CLICK_LINUXMODULE
    // set state to interruptible early to avoid race conditions
    set_current_state(TASK_INTERRUPTIBLE);
#endif
    driver_unlock_tasks();
#if HAVE_ADAPTIVE_SCHEDULER
    Timestamp t_before = Timestamp::now();
#endif

#if CLICK_USERLEVEL
    select_set().run_selects(this);
#elif CLICK_MINIOS
    /*
     * MiniOS uses a cooperative scheduler. By schedule() we'll give a chance
     * to the OS threads to run.
     */
    schedule();
#elif CLICK_LINUXMODULE         /* Linux kernel module */
    if (_greedy) {
        if (time_after(jiffies, greedy_schedule_jiffies + 5 * CLICK_HZ)) {
            greedy_schedule_jiffies = jiffies;
            goto short_pause;
        }
    } else if (active()) {
      short_pause:
        set_thread_state(S_PAUSED);
        set_current_state(TASK_RUNNING);
        schedule();
    } else if (_id != 0) {
      block:
        set_thread_state(S_BLOCKED);
        schedule();
    } else if (Timestamp wait = timer_set().timer_expiry_steady_adjusted()) {
        wait -= Timestamp::now_steady();
        if (!(wait > Timestamp(0, Timestamp::subsec_per_sec / CLICK_HZ)))
            goto short_pause;
        set_thread_state(S_TIMERWAIT);
        if (wait.sec() >= LONG_MAX / CLICK_HZ - 1)
            (void) schedule_timeout(LONG_MAX - CLICK_HZ - 1);
        else
            (void) schedule_timeout(wait.jiffies() - 1);
    } else
        goto block;
#elif defined(CLICK_BSDMODULE)
    if (_greedy)
        /* do nothing */;
    else if (active()) {        // just schedule others for a moment
        set_thread_state(S_PAUSED);
        yield(curthread, NULL);
    } else {
        set_thread_state(S_BLOCKED);
        _sleep_ident = &_sleep_ident;   // arbitrary address, != NULL
        tsleep(&_sleep_ident, PPAUSE, "pause", 1);
        _sleep_ident = NULL;
    }
#else
# error "Compiling for unknown target."
#endif

#if HAVE_ADAPTIVE_SCHEDULER
    client_update_pass(C_KERNEL, t_before);
#endif
    driver_lock_tasks();
}

void
RouterThread::process_pending()
{
    // must be called with thread's lock acquired

    // claim the current pending list
    set_thread_state(RouterThread::S_RUNPENDING);
    SpinlockIRQ::flags_t flags = _pending_lock.acquire();
    Task::Pending my_pending = _pending_head;
    _pending_head.x = 0;
    _pending_tail = &_pending_head;
    _pending_lock.release(flags);

    // process the list
    while (my_pending.x > 2) {
        Task *t = my_pending.t;
        my_pending = t->_pending_nextptr;
        t->process_pending(this);
    }
}

void
RouterThread::cmd_driver() {

    _driver_entered = true;
#if CLICK_LINUXMODULE
    // this task is running the driver
    _linux_task = current;
#elif CLICK_USERLEVEL
    select_set().initialize();
# if CLICK_USERLEVEL && HAVE_MULTITHREAD
    _running_processor = click_current_processor();
#  if HAVE___THREAD_STORAGE_CLASS
    click_current_thread_id = _id | 0x40000000;
#  endif
# endif
#endif

    MsgQueue* msg_queue = master()->get_msg_queue();

    while(1) {
        msg_queue->lock();
        while(msg_queue->empty()) {
            msg_queue->wait();
        }
        Message msg = msg_queue->get_message();
        msg_queue->unlock();
        int ret = -1;
        // printf("%s : %s\n", msg.cmd.mutable_data(), msg.arg.mutable_data());
        if(msg.cmd == "addnf") {
            ret = add_nf(msg.arg);
        } else if(msg.cmd == "delnf") {
            ret = delete_nf(msg.arg);
        } else if(msg.cmd == "movenf") {
            ret = move_nf(msg.arg);
        } else if(msg.cmd == "move_reset_nf") {
            ret = move_reset_nf(msg.arg);
        } else if(msg.cmd == "balance") {
            ret = balance(msg.arg);
        } else if(msg.cmd == "newbalance") {
            ret = newbalance(msg.arg);
        } else if(msg.cmd == "randombalance") {
            ret = randombalance(msg.arg);
        } else if(msg.cmd == "dividebalance") {
        	ret = dividebalance(msg.arg);
		} else if(msg.cmd == "addthread") {
            ret = add_thread(msg.arg);
        } else if (msg.cmd == "global") {
            ret = global(msg.arg);
        }
        master()->set_msg_status(msg.id, (ret==-1 ? -1 : 1));
    }
}

void
RouterThread::driver()
{
    int iter = 0;
    _driver_entered = true;
#if CLICK_LINUXMODULE
    // this task is running the driver
    _linux_task = current;
#elif CLICK_USERLEVEL
    select_set().initialize();
# if CLICK_USERLEVEL && HAVE_MULTITHREAD
    _running_processor = click_current_processor();
#  if HAVE___THREAD_STORAGE_CLASS
    click_current_thread_id = _id | 0x40000000;
#  endif
# endif
#endif

#if CLICK_NS
    {
        Timestamp now = Timestamp::now();
        if (now >= _ns_scheduled)
            _ns_scheduled.set_sec(-1);
    }
#endif

    driver_lock_tasks();

#if HAVE_ADAPTIVE_SCHEDULER
    client_set_tickets(C_CLICK, DRIVER_TOTAL_TICKETS / 2);
    client_set_tickets(C_KERNEL, DRIVER_TOTAL_TICKETS / 2);
    _cur_click_share = Task::MAX_UTILIZATION / 2;
    _adaptive_restride_timestamp.assign_now();
    _adaptive_restride_iter = 0;
#endif

    while (1) {
#if CLICK_DEBUG_SCHEDULING
        _driver_epoch++;
#endif

#if !BSD_NETISRSCHED
        // check to see if driver is stopped
        if (_stop_flag && _master->verify_stop(this))
            break;
#endif

        // run occasional tasks: timers, select, etc.
        iter++;

        // run task requests
        click_compiler_fence();
        if (_pending_head.x)
            process_pending();

        // run tasks
        do {
#if HAVE_ADAPTIVE_SCHEDULER
            if (PASS_GT(_clients[C_CLICK].pass, _clients[C_KERNEL].pass))
                break;
#endif
            run_tasks(_tasks_per_iter);
        } while (0);

#if CLICK_USERLEVEL
        // run signals
        run_signals();
#endif

        // run timers
        do {
#if !BSD_NETISRSCHED
            if (iter % timer_set().timer_stride())
                break;
#elif BSD_NETISRSCHED
            if (iter % timer_set().timer_stride() && _oticks == ticks)
                break;
            _oticks = ticks;
#endif
            timer_set().run_timers(this, _master);
        } while (0);

        // run operating system
        do {
#if !HAVE_ADAPTIVE_SCHEDULER && !BSD_NETISRSCHED
            if (iter % _iters_per_os)
                break;
#elif HAVE_ADAPTIVE_SCHEDULER
            if (!PASS_GT(_clients[C_CLICK].pass, _clients[C_KERNEL].pass))
                break;
#elif BSD_NETISRSCHED
            break;
#endif
            run_os();
        } while (0);

#if CLICK_NS || BSD_NETISRSCHED
        // Everyone except the NS driver stays in driver() until the driver is
        // stopped.
        break;
#endif
    }

    driver_unlock_tasks();

    _driver_entered = false;
#if HAVE_ADAPTIVE_SCHEDULER
    _cur_click_share = 0;
#endif
#if CLICK_LINUXMODULE
    _linux_task = 0;
#endif
#if CLICK_USERLEVEL && HAVE_MULTITHREAD
    _running_processor = click_invalid_processor();
# if HAVE___THREAD_STORAGE_CLASS
    click_current_thread_id = 0;
# endif
#endif

#if CLICK_NS
    do {
        // Set an NS timer for the next time to run Click
        Timestamp t = Timestamp::uninitialized_t();
        if (active()) {
            t = Timestamp::now();
            if (t != _ns_last_active) {
                _ns_active_iter = 0;
                _ns_last_active = t;
            } else if (++_ns_active_iter >= ns_iters_per_time)
                t += Timestamp::epsilon();
        } else if (Timestamp next_expiry = timer_set().timer_expiry_steady())
            t = next_expiry;
        else
            break;
        if (t >= _ns_scheduled && _ns_scheduled.sec() >= 0)
            break;
        if (Timestamp::schedule_granularity == Timestamp::usec_per_sec) {
            t = t.usec_ceil();
            struct timeval tv = t.timeval();
            simclick_sim_command(_master->simnode(), SIMCLICK_SCHEDULE, &tv);
        } else {
            t = t.nsec_ceil();
            struct timespec ts = t.timespec();
            simclick_sim_command(_master->simnode(), SIMCLICK_SCHEDULE, &ts);
        }
        _ns_scheduled = t;
    } while (0);
#endif
}


void
RouterThread::kill_router(Router *r)
{
    assert(r->dying());
    lock_tasks();
#if HAVE_TASK_HEAP
    Task *t;
    for (task_heap_element *tp = _task_heap.end(); tp > _task_heap.begin(); )
        if ((t = (--tp)->t, t->router() == r)) {
            task_reheapify_from(tp - _task_heap.begin(), _task_heap.back().t);
            // must clear _schedpos AFTER task_reheapify_from
            t->_schedpos = -1;
            // recheck this slot; have moved a task there
            _task_heap.pop_back();
            if (tp < _task_heap.end())
                tp++;
        }
#else
    TaskLink *prev = &_task_link;
    TaskLink *t;
    for (t = prev->_next; t != &_task_link; t = t->_next)
        if (static_cast<Task *>(t)->router() == r)
            t->_prev = 0;
        else {
            prev->_next = t;
            t->_prev = prev;
            prev = t;
        }
    prev->_next = t;
    t->_prev = prev;
#endif
    click_compiler_fence();
    if (_pending_head.x)
        process_pending();
    unlock_tasks();

    _timers.kill_router(r);
#if CLICK_USERLEVEL
    _selects.kill_router(r);
#endif
}

#if CLICK_DEBUG_SCHEDULING
String
RouterThread::thread_state_name(int ts)
{
    switch (ts) {
    case S_PAUSED:              return String::make_stable("paused");
    case S_BLOCKED:             return String::make_stable("blocked");
    case S_TIMERWAIT:           return String::make_stable("timerwait");
    case S_LOCKSELECT:          return String::make_stable("lockselect");
    case S_LOCKTASKS:           return String::make_stable("locktasks");
    case S_RUNTASK:             return String::make_stable("runtask");
    case S_RUNTIMER:            return String::make_stable("runtimer");
    case S_RUNSIGNAL:           return String::make_stable("runsignal");
    case S_RUNPENDING:          return String::make_stable("runpending");
    case S_RUNSELECT:           return String::make_stable("runselect");
    default:                    return String(ts);
    }
}
#endif

int
RouterThread::add_nf(String config_file) {
    config_file.trim_space();
    Router* router = click_read_router(config_file, false, NULL, false, master());
    router->initialize(ErrorHandler::silent_handler());
    String router_name = router->router_info()->router_name();    
    router->activate(ErrorHandler::default_handler());
    master()->lock_write();
    master()->_router_map.insert(router_name, router);
    master()->unlock_rw();
    printf("router %s activated\n", router->router_info()->router_name().mutable_c_str());
    printf("number of tasks: %d\n", router->_tasks.size());
    return 0;
}

int
RouterThread::delete_nf(String router_name) {
    Router* router = master()->get_router(router_name);
    if(!router) {
        return -1;
    }

    driver_lock_tasks();

    // move tasks
    for(int i=0; i<router->_tasks.size(); ++i) {
        router->_tasks[i]->kill(_id);
    }

    while(1) {
        Task::Pending my_pending = _pending_head;
        // process the list
        int count = 0;
        while (my_pending.x > 2) {
            Task *t = my_pending.t;
            my_pending = t->_pending_nextptr;
            ++count;
        }
        if(count == router->_tasks.size())
            break;
        run_os();
    }

    driver_unlock_tasks();

    _pending_head.x = 0;
    _pending_tail = &_pending_head;

    Master* m = master();
    // router->_running = Router::RUNNING_DEAD;
    // router->unuse(); bug
    m->_unused_tasks.push_back(router);
    m->lock_master();
    Router **pprev = &(m->_routers);
    for (Router *r = *pprev; r; r = r->_next_router) {
       if (r != router) {
           *pprev = r;
           pprev = &r->_next_router;
        }
    }
    m->_refcount--;  
    m->unlock_master();

    m->lock_write();
    m->_router_map.remove(router_name);
    m->unlock_rw();
    printf("delete router %s\n", router_name.mutable_data());

    return 0;
}

void
RouterThread::reset_element(String name) {
    Router* r;
    String sysRouter("sys");
    for(HashMap<String, Router*>::iterator it = master()->_router_map.begin(); it.live(); it++) {
        if(it.key().equals(sysRouter)) continue;
        r = it.value();
        break;
    }
    RouterInfo *ri = r->router_info();
    ri->reset_element(name);
}

String
get_reset_name(String& info, int& start) {
    String who;
    int pos = start, len = info.length(), first;
    while (pos < len && isspace((unsigned char) info[pos]))
      pos++;
    first = pos;
    while (pos < len && !isspace((unsigned char) info[pos]))
      pos++;
    who = info.substring(first, pos - first).unshared();
    start = pos;
    return who;
}

int
RouterThread::move_reset_nf(String info) {
    int pos = 0, len = info.length();
    String reset_name = get_reset_name(info, pos);
    while (pos < len) {
        pos = help_move_nf(info, pos);
    }
    reset_element(reset_name);

    return 0;
}

int
RouterThread::move_nf(String info) {
    int pos = 0, len = info.length();
    while (pos < len) {
        pos = help_move_nf(info, pos);
    }

    return 0;
}

int
RouterThread::help_move_nf(String& info, int start) {
    String who;
    int where = 0;
    int pos = start, len = info.length(), first;
    while (pos < len && isspace((unsigned char) info[pos]))
      pos++;
    first = pos;
    while (pos < len && !isspace((unsigned char) info[pos]))
      pos++;
    who = info.substring(first, pos - first);
    while (pos < len && isspace((unsigned char) info[pos]))
      pos++;
    first = pos;
    while (pos < len && !isspace((unsigned char) info[pos]))
      pos++;
    IntArg().parse(info.substring(first, pos - first), where);

    const char *dot1 = find(who, '.');
    String rname = who.substring(who.begin(), dot1);
    String ename = who.substring(dot1+1, who.end());

    Router* r = master()->get_router(rname);
    Element* e = r->find(ename);
    Task* t = 0;
    for(int i=0; i<r->_tasks.size(); ++i) {
        if(r->_tasks[i]->element() == e) {
            t = r->_tasks[i];
            break;
        }
    }
    t->move_thread(where);

    std::cout << "move "
              << t->element()->name().c_str()
              << " to "
              << where
              << std::endl;

    return pos;
}

static int task_increasing_sorter(const void *va, const void *vb, void *) {
    Task **a = (Task **)va, **b = (Task **)vb;
    int ca = (*a)->cycles(), cb = (*b)->cycles();
    return (ca < cb ? -1 : (cb < ca ? 1 : 0));
}

static int task_decreasing_sorter(const void *va, const void *vb, void *) {
    Task **a = (Task **)va, **b = (Task **)vb;
    double ca = (*a)->_task_load, cb = (*b)->_task_load;
    return (ca < cb ? 1 : (cb < ca ? -1 : 0));
}

int
RouterThread::balance(String nullstr) {
    // index for thread starts from 1
    int cpuNum = master()->run_nthreads();
    Vector<Task*> tasks;
    Vector<int> cycles;
    Vector<int> rates;
    Vector<double> oldTaskLoads;
    Vector<double> oldCpuLoads(cpuNum+1, 0);
    for(HashMap<String, Router*>::iterator it = master()->_router_map.begin(); it.live(); it++) {
        Vector<Task*>& ts = it.value()->_tasks;
        for(int i=0; i<ts.size(); i++) {
            tasks.push_back(ts[i]);
            cycles.push_back(ts[i]->cycles());
            rates.push_back(ts[i]->rates());
            oldTaskLoads.push_back((double)cycles[i] * (double)rates[i]);
            int tid = ts[i]->home_thread_id();
            oldCpuLoads[tid] += oldTaskLoads[i];
            ts[i]->_task_load = oldTaskLoads[i];
        }
    }
    for(int i=0; i<tasks.size(); ++i) {
        std::cout << tasks[i]->element()->name().c_str() << ": "
                  << "cycle " << cycles[i]
                  << ", rate " << rates[i]
                  << std::endl;
    }
    for(int i=0; i<oldCpuLoads.size(); i++) {
        std::cout << oldCpuLoads[i] << std::endl;
    }

    double oldAvgCpuLoad = 0, oldCpuBalance = 0;
    for(int i=1; i<=cpuNum; i++) {
        oldAvgCpuLoad += oldCpuLoads[i];
    }
    oldAvgCpuLoad = oldAvgCpuLoad / cpuNum;
    for(int i=1; i<=cpuNum; i++) {
        oldCpuBalance += (oldCpuLoads[i] - oldAvgCpuLoad) * (oldCpuLoads[i] - oldAvgCpuLoad);
    }
    oldCpuBalance = std::sqrt(oldCpuBalance/cpuNum);
    std::cout << "old balance: " << oldCpuBalance << std::endl;

    Vector<Task*> sortedTasks = tasks;
    Vector<int> allocThread;
    Task **tbegin = sortedTasks.begin();
    click_qsort(tbegin, sortedTasks.size(), sizeof(Task *), task_decreasing_sorter);

    Vector<double> newCpuLoads(cpuNum+1, 0);
    for(int i=0; i<sortedTasks.size(); i++) {
        int id = 1;
        for(int j=1; j<=cpuNum; j++) {
            if(newCpuLoads[j] < newCpuLoads[id]) {
                id = j;
            }
        }
        newCpuLoads[id] += sortedTasks[i]->_task_load;
        allocThread.push_back(id);
    }

    double newAvgCpuLoad = 0, newCpuBalance = 0;
    for(int i=1; i<=cpuNum; i++) {
        newAvgCpuLoad += newCpuLoads[i];
    }
    newAvgCpuLoad = newAvgCpuLoad / cpuNum;
    for(int i=1; i<=cpuNum; i++) {
        newCpuBalance += (newCpuLoads[i] - newAvgCpuLoad) * (newCpuLoads[i] - newAvgCpuLoad);
    }
    newCpuBalance = std::sqrt(newCpuBalance/cpuNum);
    std::cout << "new balance: " << newCpuBalance << std::endl;

    for(int i=0; i<sortedTasks.size(); i++) {
        Element *ele = sortedTasks[i]->element();
        std::cout << ele->name().c_str() << " from "
                  << sortedTasks[i]->home_thread_id() << " to "
                  << allocThread[i]
                  << std::endl;
    }

    // for(int i=0; i<sortedTasks.size(); i++) {
    //     sortedTasks[i]->move_thread(allocThread[i]);
    // }

   return 0;
}

int
RouterThread::randombalance(String sth) {
    int startThread = 1;
    IntArg().parse(sth, startThread);

    // index for thread starts from 1
    int cpuNum = master()->run_nthreads();
    int validCpuNum = cpuNum - startThread + 1;
    Vector<Task*> tasks;

    String sysRouter("sys");
    std::cout << "======================== random balance ========================" << std::endl;
    for(HashMap<String, Router*>::iterator it = master()->_router_map.begin(); it.live(); it++) {
        if(it.key().equals(sysRouter)) continue;
        std::cout << "Router: " << it.key().c_str() << std::endl;
        Router* r = it.value();
        RouterInfo *ri = r->router_info();
        Vector<Task*>& t = ri->task();
        ri->update_info();
        for(int i=0; i<t.size(); i++) {
            tasks.push_back(t[i]);
        }
    }

    Vector<int> allocThread;
    srand(time(NULL)); 
    for(int i=0; i<tasks.size(); i++) {
        int id = rand() % validCpuNum + startThread;
        allocThread.push_back(id);
    }

    std::cout << "2222222222222222 policy 2222222222222222" << std::endl;
    HashMap<Router*, String> policy;
    for(int i=0; i<tasks.size(); i++) {
        Element *ele = tasks[i]->element();
        Router *r = ele->router();
        std::stringstream ss;
        ss << ele->name().c_str() << " from " << tasks[i]->home_thread_id()
                        << " to " << allocThread[i] << "\n";
        String* sp = policy.findp(r);
        if(sp == 0) {
            String val(ss.str().c_str());
            policy.insert(r, val);
        } else {
            (*sp) += ss.str().c_str();
        }
    }
    for(HashMap<Router*, String>::const_iterator it=policy.begin(); it.live(); it++) {
        Router* r = it.key();
        std::cout << "Router: " << r->router_info()->router_name().c_str() << std::endl;
        std::cout << it.value().c_str() << std::endl;
    }

    for(int i=0; i<tasks.size(); i++) {
        tasks[i]->move_thread(allocThread[i]);
    }

   return 0;
}

int
RouterThread::global(String sth) {
    std::cout << "======================== global balance ========================" << std::endl;

    bool move = false;
    BoolArg().parse(sth, move);
    String sysRouter("sys");
    for(HashMap<String, Router*>::iterator it = master()->_router_map.begin(); it.live(); it++) {
        if(it.key().equals(sysRouter)) continue;
        std::cout << "Router: " << it.key().c_str() << std::endl;
        Router* r = it.value();
        RouterInfo *ri = r->router_info();
        ri->update_chain(move);
    }

   return 0;
}

int
RouterThread::newbalance(String sth) {
    int startThread = 1;
    IntArg().parse(sth, startThread);

    // index for thread starts from 1
    int cpuNum = master()->run_nthreads();
    int validCpuNum = cpuNum - startThread + 1;
    Vector<Task*> tasks;
    Vector<int> cycles;
    Vector<double> rates;
    Vector<double> oldTaskLoads;
    Vector<double> oldCpuLoads(cpuNum+1, 0);
    double totalCpuLoad=0, avgCpuLoad=0;

    HashMap<Router*, double> srcRate;
    double totalSrcRate = 0.0;
	String sysRouter("sys");
    std::cout << "======================== newbalance ========================" << std::endl;
    std::cout << "111111111111111 update information 111111111111111" << std::endl;
    for(HashMap<String, Router*>::iterator it = master()->_router_map.begin(); it.live(); it++) {
		if(it.key().equals(sysRouter)) continue;
		std::cout << "Router: " << it.key().c_str() << std::endl;
        Router* r = it.value();
        RouterInfo *ri = r->router_info();
        ri->update_info();
        double rate = ri->src_rate();
        totalSrcRate += rate;
        srcRate.insert(r, rate);
    }

    std::cout << "2222222222222222 cycle and rate 222222222222222" << std::endl;
    for(HashMap<String, Router*>::iterator it = master()->_router_map.begin(); it.live(); it++) {
        if(it.key().equals(sysRouter)) continue;
		std::cout << "Router: " << it.key().c_str() << std::endl;
        Router* r = it.value();
        RouterInfo *ri = r->router_info();
        Vector<Task*>& t = ri->task();
        Vector<int>& c = ri->task_cycle();
        Vector<double>& rate = ri->task_rate(srcRate[r]/totalSrcRate);
        for(int i=0; i<t.size(); i++) {
            tasks.push_back(t[i]);
            cycles.push_back(c[i]);
            rates.push_back(rate[i]);
            std::cout << "(" << t[i]->element()->name().c_str() << ", " << cycles[i] << ", " << rates[i] << ", " << cycles[i]*rates[i] <<  ")";
        }
        std::cout << std::endl;
    }

    std::cout << "3333333333333333 current cpu load 3333333333333333" << std::endl;
    for(int i=0; i<tasks.size(); i++) {
        oldTaskLoads.push_back(cycles[i] * rates[i]);
        int tid = tasks[i]->home_thread_id();
        oldCpuLoads[tid] += oldTaskLoads[i];
        tasks[i]->_task_load = oldTaskLoads[i];
        totalCpuLoad += oldTaskLoads[i];
    }
    avgCpuLoad = totalCpuLoad / validCpuNum;

    double oldCpuBalance = 0;
    for(int i=startThread; i<=cpuNum; i++) {
        oldCpuBalance += (oldCpuLoads[i] - avgCpuLoad) * (oldCpuLoads[i] - avgCpuLoad);
    }
    oldCpuBalance = std::sqrt(oldCpuBalance/validCpuNum);

    std::cout << "CPU load: ";
    for(int i=startThread; i<=cpuNum; i++) {
        std::cout << "(" << i << ", " << oldCpuLoads[i] << ") "; 
    }
    std::cout << "\nCPU load balance: " << oldCpuBalance << std::endl;

    std::cout << "4444444444444444 balance cpu load 4444444444444444" << std::endl;
    Vector<Task*> sortedTasks = tasks;
    Task **tbegin = sortedTasks.begin();
    click_qsort(tbegin, sortedTasks.size(), sizeof(Task *), task_decreasing_sorter);

    Vector<int> allocThread;
    Vector<double> newCpuLoads(cpuNum+1, 0);
    for(int i=0; i<sortedTasks.size(); i++) {
        int id = startThread;
        for(int j=startThread; j<=cpuNum; j++) {
            if(newCpuLoads[j] < newCpuLoads[id]) {
                id = j;
            }
        }
        newCpuLoads[id] += sortedTasks[i]->_task_load;
        allocThread.push_back(id);
    }

    double newCpuBalance = 0;
    for(int i=startThread; i<=cpuNum; i++) {
        newCpuBalance += (newCpuLoads[i] - avgCpuLoad) * (newCpuLoads[i] - avgCpuLoad);
    }
    newCpuBalance = std::sqrt(newCpuBalance/validCpuNum);
    
    std::cout << "CPU load: ";
    for(int i=startThread; i<=cpuNum; i++) {
        std::cout << "(" << i << ", " << newCpuLoads[i] << ") "; 
    }
    std::cout << "\nCPU load balance: " << newCpuBalance << std::endl;


    std::cout << "5555555555555555 policy 5555555555555555" << std::endl;
    HashMap<Router*, String> policy;
    for(int i=0; i<sortedTasks.size(); i++) {
        Element *ele = sortedTasks[i]->element();
        Router *r = ele->router();
        std::stringstream ss;
        ss << ele->name().c_str() << " from " << sortedTasks[i]->home_thread_id()
                        << " to " << allocThread[i] << "\n";
        String* sp = policy.findp(r);
		if(sp == 0) {
			String val(ss.str().c_str());
			policy.insert(r, val);
		} else {
			(*sp) += ss.str().c_str();
		}
    }
    for(HashMap<Router*, String>::const_iterator it=policy.begin(); it.live(); it++) {
        Router* r = it.key();
        std::cout << "Router: " << r->router_info()->router_name().c_str() << std::endl;
        std::cout << it.value().c_str() << std::endl;
    }

    for(int i=0; i<sortedTasks.size(); i++) {
    	sortedTasks[i]->move_thread(allocThread[i]);
    }

   return 0;
}

void
RouterThread::subbalance(const Vector<Task*>& tasks, const Vector<double>& rates, const Vector<int>& cycles, int start, int end) {
    int cpuNum = end;
    int validCpuNum = cpuNum - start + 1;
    Vector<double> oldTaskLoads;
    Vector<double> newCpuLoads(cpuNum+1, 0);
    Vector<int> allocThread;
    double totalCpuLoad=0, avgCpuLoad=0;

    for(int i=0; i<tasks.size(); i++) {
        oldTaskLoads.push_back(cycles[i] * rates[i]);
        tasks[i]->_task_load = oldTaskLoads[i];
        totalCpuLoad += oldTaskLoads[i];
    }
    avgCpuLoad = totalCpuLoad / validCpuNum;

    Vector<Task*> sortedTasks = tasks;
    Task **tbegin = sortedTasks.begin();
    click_qsort(tbegin, sortedTasks.size(), sizeof(Task *), task_decreasing_sorter);

    for(int i=0; i<sortedTasks.size(); i++) {
        int id = start;
        for(int j=start; j<=cpuNum; j++) {
            if(newCpuLoads[j] < newCpuLoads[id]) {
                id = j;
            }
        }
        newCpuLoads[id] += sortedTasks[i]->_task_load;
        allocThread.push_back(id);
    }

    double newCpuBalance = 0;
    for(int i=start; i<=cpuNum; i++) {
        newCpuBalance += (newCpuLoads[i] - avgCpuLoad) * (newCpuLoads[i] - avgCpuLoad);
    }
    newCpuBalance = std::sqrt(newCpuBalance/validCpuNum);
    
    for(int i=0; i<tasks.size(); i++) {
        std::cout << "(" << tasks[i]->element()->name().c_str() << ", " << cycles[i] << ", " << rates[i] << ", " << cycles[i]*rates[i] <<  ")";
    }
    std::cout << std::endl;

    std::cout << "CPU load: ";
    for(int i=start; i<=cpuNum; i++) {
        std::cout << "(" << i << ", " << newCpuLoads[i] << ") "; 
    }
    std::cout << "\nCPU load balance: " << newCpuBalance << std::endl;

    for(int i=0; i<sortedTasks.size(); i++) {
        Element *ele = sortedTasks[i]->element();
        std::cout << ele->name().c_str() << " from " << sortedTasks[i]->home_thread_id()
                        << " to " << allocThread[i] << std::endl;
        sortedTasks[i]->move_thread(allocThread[i]);
    }
}


int
RouterThread::dividebalance(String sth) {
    int startThread = 1;
    IntArg().parse(sth, startThread);

    // index for thread starts from 1
    int cpuNum = master()->run_nthreads();
    int validCpuNum = cpuNum - startThread + 1;
    Vector<Router*> routers;
    Vector<double> routerLoads;
    Vector<int> routerThreads;
    double totalLoads = 0;
    HashMap<Router*, Vector<Task*>> tasks;
    HashMap<Router*, Vector<int>> cycles;
    HashMap<Router*, Vector<double>> rates;
    HashMap<Router*, double> routerLoad;

    String sysRouter("sys");
    std::cout << "======================== divide balance ========================" << std::endl;
    std::cout << "111111111111111 update information 111111111111111" << std::endl;
    for(HashMap<String, Router*>::iterator it = master()->_router_map.begin(); it.live(); it++) {
        if(it.key().equals(sysRouter)) continue;
        std::cout << "Router: " << it.key().c_str() << std::endl;
        Router* r = it.value();
        RouterInfo *ri = r->router_info();
        ri->update_info();
    }

    std::cout << "2222222222222222 cycle and rate 222222222222222" << std::endl;
    for(HashMap<String, Router*>::iterator it = master()->_router_map.begin(); it.live(); it++) {
        if(it.key().equals(sysRouter)) continue;
        std::cout << "Router: " << it.key().c_str() << std::endl;
        Router* r = it.value();
        routers.push_back(r);
        RouterInfo *ri = r->router_info();
        Vector<Task*>& t = ri->task();
        Vector<int>& c = ri->task_cycle();
        Vector<double>& rate = ri->task_rate(1.0);
        tasks.insert(r, t);
        cycles.insert(r, c);
        rates.insert(r, rate);
		double ll = 0;
        for(int i=0; i<t.size(); i++) {
        	double load = c[i] * rate[i];
			ll += load;
            std::cout << "(" << t[i]->element()->name().c_str() << ", " << c[i] << ", " << rate[i] << ", " << load << ")";
        }
        routerLoads.push_back(ll);
        totalLoads += ll;
        std::cout << std::endl;       
    }
	std::cout << "Router loads" << std::endl;
	for(int i=0; i<routers.size(); i++) {
		std::cout << "("<< routers[i]->router_name().c_str() << ", " << routerLoads[i] << ")";
	}
	std::cout << std::endl;
    std::cout << "3333333333333333 divide cpu 3333333333333333" << std::endl;
    int nRouters = routers.size();
    int nLeftCpuNum = validCpuNum;
    Vector<double> tmpRouterLoads = routerLoads;
    routerThreads.resize(nRouters, 0);
    while(true) {
        int tmpNRouters = 0;
        double tmpTotalLoads = 0;
        for(int i=0; i<nRouters; i++) {
            if(tmpRouterLoads[i] < 0) continue;
            tmpTotalLoads += tmpRouterLoads[i];
            ++tmpNRouters;
        }
        int leftRouters = tmpNRouters;
        for(int i=0; i<nRouters; i++) {
            if(tmpRouterLoads[i] < 0) continue;
            double k = tmpRouterLoads[i] / tmpTotalLoads * nLeftCpuNum;
            if(k <= 1) {
                routerThreads[i] = 1;
                --nLeftCpuNum; 
                tmpRouterLoads[i] = -1;
                --leftRouters;
            } else {
                routerThreads[i] = k;
            }
        }
        if(leftRouters == tmpNRouters) {
            break;
        }
    }
	// for(int i=0; i<nRouters; i++) {
	// 	std::cout << "(" << routers[i]->router_name().c_str() << ", " << tmpRouterLoads[i] << ", " << routerThreads[i] << ")";
	// }
	std::cout << std::endl;
	Vector<double> ratio(nRouters, -1);
    for(int i=0; i<nRouters; i++) {
        if(tmpRouterLoads[i] < 0) continue;
        ratio[i] = (routerThreads[i] - (int)routerThreads[i]) / routerThreads[i];
    }
    while(true) {
        int m1=-1, m2=-1;
        for(int i=0; i<nRouters; i++) {
            if(tmpRouterLoads[i] > 0) {
                m1 = m2 = i;
                break;
            }
        }
        if(m1 == -1) {
            break;
        }
        for(int i=m1+1; i<nRouters; i++) {
            if(tmpRouterLoads[i] < 0) continue;
            if(ratio[i] > ratio[m2]) {
                m2 = i;
            }
            if(ratio[i] < ratio[m1]) {
                m1 = i;
            }
        }
		tmpRouterLoads[m1] = -1;
		tmpRouterLoads[m2] = -1;
        if(m1 == m2) {
            routerThreads[m1] = nLeftCpuNum;
            nLeftCpuNum = 0;
            break;
        }
        routerThreads[m2] = (int)(routerThreads[m2]+1);
        routerThreads[m1] = (int)routerThreads[m1];
        nLeftCpuNum -= (routerThreads[m1] + routerThreads[m2]);
    }

    for(int i=0; i<nRouters; ++i) {
        std::cout << "("<< routers[i]->router_name().c_str() << ", " << routerThreads[i] << ")";
    }
    std::cout << std::endl;

    std::cout << "4444444444444444 router balance 4444444444444444" << std::endl;
    int tmpStart = startThread;
    for(int i=0; i<routers.size(); i++) {
        Router* r = routers[i];
        std::cout << "========= " << r->router_name().c_str() << " =========" << std::endl;
        int end = tmpStart + routerThreads[i];
        subbalance(tasks[r], rates[r], cycles[r], tmpStart, end-1);
        tmpStart = end;
    }

	return 0;
}

int
RouterThread::add_thread(String nstr) {
    int num = 0;
    IntArg().parse(nstr, num);
    for(int i=0; i<num; ++i) {
        int tid = master()->add_thread();
        if(tid < 0)
            break;
    }
    return 0;
}

CLICK_ENDDECLS
