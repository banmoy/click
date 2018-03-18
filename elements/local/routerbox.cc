#include <click/config.h>
#include <click/args.hh>
#include "routerbox.hh"
#include <click/router.hh>
#include "elements/standard/fullnotequeue.hh"
#include <iostream>
#include <queue>
CLICK_DECLS

RouterBox::RouterBox()
    : _task_id(0)
{}

RouterBox::~RouterBox()
{}

int
RouterBox::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String topo = "";
    if (Args(conf, this, errh)
        .read_mp("NAME", _router_name)
        .read_p("TOPOLOGY", topo)
        .complete() < 0)
        return -1;
    router()->set_router_info(this);
    _topo = topo;
	if(_topo.length())
		setup_topology();
    return 0;
}

void
RouterBox::setup_topology() {
    int i = 0;
    while(i<_topo.length()) {
        int j=i;
        while(_topo[j] != ',')
            ++j;
        String task = _topo.substring(i, j-i).unshared();
        _task_id.insert(task, _id);
        _id_task.insert(_id, task);
        ++_id;
        _task_input.insert(task, Vector<String>());
        _task_input_rate.insert(task, Vector<int>());
        _task_input_cycle.insert(task, Vector<int>());
        _task_output.insert(task, Vector<String>());
        _task_output_rate.insert(task, Vector<int>());
        _task_output_cycle.insert(task, Vector<int>());
        i = ++j;
        while(_topo[j]!=',')
            ++j;
        if(i == j) {
            _src_task = task;
        }
        int k = i;
        while(k<j) {
            while(k<j && _topo[k]!=' ') 
                k++;
            String tmp = _topo.substring(i, k-i).unshared();
            _task_input.findp(task)->push_back(tmp);
            _task_input_rate.findp(task)->push_back(0);
            _task_input_cycle.findp(task)->push_back(0);
            _input_to_task.insert(tmp, task);
            i = ++k;
        }
        i = ++j;
        while(j<_topo.length() && _topo[j]!=',')
            ++j;
        k = i;
        while(k<j) {
            while(k<j && _topo[k]!=' ') 
                k++;
            String tmp = _topo.substring(i, k-i).unshared();
            _task_output.findp(task)->push_back(tmp);
            _task_output_rate.findp(task)->push_back(0);
            _task_output_cycle.findp(task)->push_back(0);
            i = ++k;
        }
        i = ++j;
    }

    // std::cout << "=================== task input ===================" << std::endl;
    // for(HashMap<String, Vector<String>>::const_iterator it = _task_input.begin();
    //         it.live(); it++) {
    //     std::cout << it.key().c_str() << ": ";
    //     const Vector<String>& input = it.value();
    //     for(int i=0; i<input.size(); ++i) {
    //         std::cout << input[i].c_str() << " ";
    //     }
    //     std::cout << std::endl;
    // }

    // std::cout << "=================== task output ===================" << std::endl;
    // for(HashMap<String, Vector<String>>::const_iterator it = _task_output.begin();
    //         it.live(); it++) {
    //     std::cout << it.key().c_str() << ": ";
    //     const Vector<String>& output = it.value();
    //     for(int i=0; i<output.size(); ++i) {
    //         std::cout << output[i].c_str() << " ";
    //     }
    //     std::cout << std::endl;
    // }

	// task-id mapping
	// std::cout << "==========================task-id mapping=====================" << std::endl;
	// for(HashMap<String, int>::const_iterator it = _task_id.begin();
	// 				it.live(); it++) {
	// 		std::cout << "(" << it.key().c_str() << ", " << it.value() << ")";
	// }
	// std::cout << std::endl;

	// for(HashMap<int, String>::const_iterator it = _id_task.begin();
	// 				it.live(); it++) {
	// 		std::cout << "(" << it.value().c_str() << ", " << it.key() << ")";
	// }
	// std::cout << std::endl;
    
    // resize rate and cycle
    _rates.resize(_id, 0);
    _raw_task_cycles.resize(_id, 0);
    _pull_cycles.resize(_id, 0);
    _push_cycles.resize(_id, 0);
    _cycles.resize(_id, 0);

   	// build adj table
    _adj_table.resize(_id);
    for(HashMap<String, Vector<String>>::const_iterator it = _task_output.begin();
            it.live(); it++) {
        const String &src = it.key();
        int srcid = _task_id.find(src);
        const Vector<String>& output = it.value();
        for(int i=0; i<output.size(); ++i) {
            const String &dst = _input_to_task[output[i]];
            int dstid = _task_id.find(dst);
            _adj_table[srcid].push_back(dstid);
        }
    }

	// adj table
	// std::cout << "=================== adj table =====================" << std::endl;
	// for(int i=0; i<_adj_table.size(); i++) {
	// 	std::cout << i << ": ";
	// 	for(int j=0; j<_adj_table[i].size(); j++) {
	// 			std::cout << _adj_table[i][j] << " ";
	// 	}
	// 	std::cout << std::endl;
	// }

    _weight.resize(_id, Vector<double>(_id, 0.0));

    // topology sort
    topology_sort();
    // std::cout << "=====================topology-sorted tasks===========================" << std::endl;
    // for(int i=0; i<_toposort_task.size(); ++i) {
    //     std::cout << _id_task[_toposort_task[i]].c_str() << " ";
    // }
    // std::cout << std::endl;

    _tasks.resize(_id);
    Vector<Task*> &tasks = Element::router()->_tasks;
    for(int i=0; i<tasks.size(); i++) {
        Task *t = tasks[i];
        int tid = _task_id[tasks[i]->element()->name()];
        for(int j=0; j<_toposort_task.size(); j++) {
            if(_toposort_task[j] == tid) {
                _tasks[j] = t;
                break;
            }
        }
    }
}

void
RouterBox::topology_sort() {
    Vector<int> degree(_id, 0);
    for(int i=0; i<_adj_table.size(); ++i) {
        for(int j=0; j<_adj_table[i].size(); ++j) {
            degree[_adj_table[i][j]]++;
        }
    }

    int srcid = _task_id[_src_task];
    std::queue<int> tq;
    tq.push(srcid);
    while(!tq.empty()) {
        int i = tq.front();
        tq.pop();
        _toposort_task.push_back(i);
        for(int j=0; j<_adj_table[i].size(); ++j) {
            int k = _adj_table[i][j];
            if(--degree[k] == 0) {
                tq.push(k);
            }
        }
    }
}

String
RouterBox::router_name()
{
    return _router_name;
}

void
RouterBox::update_info() {
    Router *r = Element::router();
    
    _tasks.resize(_id);
    Vector<Task*> &tasks = r->_tasks;
    for(int i=0; i<tasks.size(); i++) {
        Task *t = tasks[i];
        int tid = _task_id[tasks[i]->element()->name()];
        for(int j=0; j<_toposort_task.size(); j++) {
            if(_toposort_task[j] == tid) {
                _tasks[j] = t;
                break;
            }
        }
    }
    
	for(HashMap<String, Vector<String>>::const_iterator it = _task_input.begin();
            it.live(); it++) {
        String task = it.key();
        const Vector<String>& input = it.value();
        Vector<int>* cycle = _task_input_cycle.findp(task);
        Vector<int>* rate = _task_input_rate.findp(task);
        for(int i=0; i<input.size(); ++i) {
            String qname = input[i];
            FullNoteQueue* q = static_cast<FullNoteQueue *>(r->find(qname));
            (*cycle)[i] = q->pull_cycles();
            (*rate)[i] = q->pull_rate();
	   }
    }

    std::cout << "input queue information" << std::endl; 
    for(HashMap<String, Vector<String>>::const_iterator it = _task_input.begin();
            it.live(); it++) {
        String task = it.key();
        std::cout << task.c_str() << ": ";
        const Vector<String>& input = it.value();
        Vector<int>* cycle = _task_input_cycle.findp(task);
        Vector<int>* rate = _task_input_rate.findp(task);
        for(int i=0; i<input.size(); ++i) {
            std::cout << "(" << input[i].c_str() << ", " << (*cycle)[i] << ", " << (*rate)[i] << ")";
        }
        std::cout << std::endl;
    }

    for(HashMap<String, Vector<String>>::const_iterator it = _task_output.begin();
            it.live(); it++) {
        String task = it.key();
        const Vector<String>& output = it.value();
        Vector<int>* cycle = _task_output_cycle.findp(task);
        Vector<int>* rate = _task_output_rate.findp(task);
        for(int i=0; i<output.size(); ++i) {
            String qname = output[i];
            FullNoteQueue* q = static_cast<FullNoteQueue *>(r->find(qname));
            (*cycle)[i] = q->push_cycles();
            (*rate)[i] = q->push_rate();
        }
    }

    std::cout << "output queue information" << std::endl; 
    for(HashMap<String, Vector<String>>::const_iterator it = _task_output.begin();
            it.live(); it++) {
        String task = it.key();
        std::cout << task.c_str() << ": ";
        const Vector<String>& output = it.value();
        Vector<int>* cycle = _task_output_cycle.findp(task);
        Vector<int>* rate = _task_output_rate.findp(task);
        for(int i=0; i<output.size(); ++i) {
            std::cout << "(" << output[i].c_str() << ", " << (*cycle)[i] << ", " << (*rate)[i] << ")";
        }
        std::cout << std::endl;
    }

    // calculate weight
    for(int i=0; i<_id; i++) {
        for(int j=0; j<_id; j++) {
            _weight[i][j] = 0;
        }
    }
    for(HashMap<String, Vector<String>>::const_iterator it = _task_output.begin();
            it.live(); it++) {
        const String &src = it.key();
        int srcid = _task_id.find(src);
        const Vector<String>& output = it.value();
        const Vector<int>& rate = _task_output_rate[src];
        double total = 0.0;
        for(int i=0; i<rate.size(); i++) {
            total += rate[i];
        }
        for(int i=0; i<output.size(); ++i) {
            const String &dst = _input_to_task.find(output[i]);
            int dstid = _task_id.find(dst);
            _weight[srcid][dstid] = rate[i] / total;
        }
    }

    std::cout << "weight information" << std::endl;
    for(int i=0; i<_id; ++i) {
        std::cout << _id_task[i].c_str() << ": ";
        for(int j=0; j<_id; ++j) {
            std::cout << "(" << _id_task[j].c_str() << ", " << _weight[i][j] << ")";
        }
        std::cout << std::endl;
    }
    
	// cycles
    for(int i=0; i<_tasks.size(); i++) {
        int tid = _toposort_task[i];
        const String &name = _id_task[tid];
        const Vector<int> &input = _task_input_cycle[name];
        const Vector<String> &output_queue = _task_output[name];
        const Vector<int> &output = _task_output_cycle[name];
		_tasks[i]->cycles();
		_raw_task_cycles[i] = _tasks[i]->cycles();
        _pull_cycles[i] = 0;
        for(int j=0; j<input.size(); ++j) {
            _pull_cycles[i] += 1.0 * input[j] / input.size();
        }
        _push_cycles[i] = 0;
        for(int j=0; j<output.size(); ++j) {
            int k = _task_id[_input_to_task[output_queue[j]]];
            _push_cycles[i] += 1.0 * output[j] * _weight[tid][k];
        }
        // _cycles[i] = _raw_task_cycles[i] - _pull_cycles[i] - _push_cycles[i];
        _cycles[i] = _raw_task_cycles[i];
    }

    std::cout << "cycle information" << std::endl;
    for(int i=0; i<_tasks.size(); i++) {
        int tid = _toposort_task[i];
        const String &name = _id_task[tid];
        std::cout << "(" << name.c_str() << ", " << _raw_task_cycles[i]
                  << ", " << _pull_cycles[i] << ", " << _push_cycles[i]
                  << ", " << _cycles[i] << ")" << std::endl;
    }
}

Vector<Task*>&
RouterBox::task() {
    return _tasks;
}

Vector<double>&
RouterBox::task_rate(double refrate) {
    for(int i=0; i<_rates.size(); i++) {
        _rates[i] = 0;
    }
    _rates[0] = refrate;
    for(int i=0; i<_id; i++) {
        for(int j=0; j<_id; j++) {
            _rates[j] += _rates[i] * _weight[i][j];
        }
    }

    std::cout << "rate information" << std::endl;
    for(int i=0; i<_id; i++) {
        int tid = _toposort_task[i];
        const String &name = _id_task[tid];
        std::cout << "(" << name.c_str() << ", " << _rates[i] << ")";
    }
	std::cout << std::endl; 

    return _rates;
}

Vector<int>&
RouterBox::task_cycle() {
    return _cycles;
}

enum { H_TASK_THREAD, H_TASK_CALL, H_TASK_COST };

String
RouterBox::read_handler(Element *e, void *thunk)
{
    RouterBox *rb = (RouterBox *)e;
    Router* r = rb->router();
    Vector<Task*>& tasks = r->_tasks; 
    switch ((intptr_t)thunk) {
      case H_TASK_THREAD: {
        String ret;
        for(int i=0; i<tasks.size(); ++i) {
            if(i) ret += ",";
            Task* t = tasks[i];
            Element* e = t->element();
            ret += r->ename(e->eindex()) + String(":") + String(t->home_thread_id());
        }
        return ret;
      }
      case H_TASK_CALL: {
        String ret;
        for(int i=0; i<tasks.size(); ++i) {
            if(i) ret += ",";
            Task* t = tasks[i];
            Element* e = t->element();
            ret += r->ename(e->eindex()) + String(":") + String(t->total_runs());
        }
        return ret;
      }
      case H_TASK_COST: {
        String ret;
        for(int i=0; i<tasks.size(); ++i) {
            if(i) ret += ",";
            Task* t = tasks[i];
            Element* e = t->element();
            ret += r->ename(e->eindex()) + String(":") + String(t->cycles());
        }
        return ret;
      }
      default:
        return "<error>";
    }
}

double
RouterBox::src_rate() {
	double rate = 0;
	const Vector<int> &output = _task_output_rate[_src_task];
	for(int i=0; i<output.size(); i++) {
		rate += output[i];
	}
	return rate;
}

void
RouterBox::add_handlers()
{
    add_read_handler("task_thread", read_handler, H_TASK_THREAD);
    add_read_handler("task_call", read_handler, H_TASK_CALL);
    add_read_handler("task_cost", read_handler, H_TASK_COST);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(RouterBox)
