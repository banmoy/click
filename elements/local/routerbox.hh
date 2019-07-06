#ifndef CLICK_ROUTERBOX_H
#define CLICK_ROUTERBOX_H

#include <click/element.hh>
#include <click/routerinfo.hh>
#include <click/hashmap.hh>
#include <click/string.hh>
#include "elements/standard/simplequeue.hh"

class RouterBox : public Element, public RouterInfo {
public:
    RouterBox() CLICK_COLD;
    ~RouterBox() CLICK_COLD;

    const char *class_name() const { return "RouterBox"; }

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;

    String router_name();

    void update_info();

	double src_rate();

	Vector<Task*>& task();

    Vector<double>& task_rate(double srctraf);

    Vector<int>& task_cycle();

    void add_handlers() CLICK_COLD;

    int setup1(Vector<String> &conf, ErrorHandler *errh);

    void reset_element(String name);

private:
    String _router_name;
    String _topo;
    String _src_task;
    String _source;
    String _source_queue;
    double _source_rate;
    int _id;

    // task name to task id
    HashMap<String, int> _task_id;

    // task id to task name
    HashMap<int, String> _id_task;

    // adjcent matrix for topology
    Vector<Vector<int>> _adj_table;

    // topology-sorted task id
    Vector<int> _toposort_task;

    // topology-sorted task
    Vector<Task*> _tasks;

    // traffic ratio for parent task to child tasks
    Vector<Vector<double>> _weight;

    // normalizd rates for task
    Vector<double> _rates;

    // raw cycles
    Vector<int> _raw_task_cycles;

    // pull cycles from input queue
    Vector<int> _pull_cycles;

    // push cycles to output queue
    Vector<int> _push_cycles; 

    // net cycles for task
    Vector<int> _cycles;

    // task name -> [input queues]
    HashMap<String, Vector<String>> _task_input;

    // task name -> [pull rate from input queues]
    HashMap<String, Vector<int>> _task_input_rate;

    // task name -> [pull cycles from input queues]
    HashMap<String, Vector<int>> _task_input_cycle;

    // task name -> [output queues]
    HashMap<String, Vector<String>> _task_output;

    // task name -> [push rate to output queues]
    HashMap<String, Vector<int>> _task_output_rate;

    // task name -> [push cycles to output queues]
    HashMap<String, Vector<int>> _task_output_cycle;

    // mapping from input queue to task
    HashMap<String, String> _input_to_task;

    void setup_topology();

    void topology_sort();

public:
    // set up chain
    // cpu frequence
    long _cpu_freq;

    // cpu load threshold
    double _thresh;

    // start cpu
    int _start_cpu;

    // end cpu
    int _end_cpu;

    // task chain
    String _task_chain;

    // task queues
    String _task_queue;

    // microseconds
    int _check_time;

    // microseconds
    int _check_interval;

    int _drop_diff;

    int _num_task;

    int _num_queue;

    Vector<String> _task_name;

    Vector<String> _queue_name;

    Vector<long> _task_cycle;

    Vector<int> _task_rate;

    Vector<long> _task_load;

    Vector<Task*> _task_obj;

    Vector<SimpleQueue*> _queue_obj;

    Vector<int> _task_cpu;

    Vector<int> _task_move;

    Vector<double> _cpu_load;

    Vector<double> _move_cpu_load;

    bool _task_init;

    bool _queue_init;

    int setup2(Vector<String> &conf, ErrorHandler *errh);

    void setup_chain();

    void setup_queue();

    void update_chain(bool move);

    void update_local_chain(bool move);

    void check_congestion();

    void init_task();

    void init_queue();

    bool is_congestion(SimpleQueue* q);

    static String read_handler(Element*, void*) CLICK_COLD;
};

#endif
