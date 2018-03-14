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
	std::cout << "==========================task-id mapping=====================" << std::endl;
	for(HashMap<String, int>::const_iterator it = _task_id.begin();
					it.live(); it++) {
			std::cout << "(" << it.key().c_str() << ", " << it.value() << ")";
	}
	std::cout << std::endl;

	for(HashMap<int, String>::const_iterator it = _id_task.begin();
					it.live(); it++) {
			std::cout << "(" << it.value().c_str() << ", " << it.key() << ")";
	}
	std::cout << std::endl;
    
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
	std::cout << "=================== adj table =====================" << std::endl;
	for(int i=0; i<_adj_table.size(); i++) {
		std::cout << i << ": ";
		for(int j=0; j<_adj_table[i].size(); j++) {
				std::cout << _adj_table[i][j] << " ";
		}
		std::cout << std::endl;
	}

    _weight.resize(_id, Vector<double>(_id, 0.0));

    // topology sort
    topology_sort();
    std::cout << "=====================topology-sorted tasks===========================" << std::endl;
    for(int i=0; i<_toposort_task.size(); ++i) {
        std::cout << _id_task[_toposort_task[i]].c_str() << " ";
    }
    std::cout << std::endl;
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
RouterBox::update_topology() {
    Router *r = Element::router();
    
    std::cout << "=========queue input information===========" << std::endl; 
    for(HashMap<String, Vector<String>>::const_iterator it = _task_input.begin();
            it.live(); it++) {
        String task = it.key();
        std::cout << task.c_str() << ": ";
        const Vector<String>& input = it.value();
        Vector<int>* cycle = _task_input_cycle.findp(task);
        Vector<int>* rate = _task_input_rate.findp(task);
        for(int i=0; i<input.size(); ++i) {
            String qname = input[i];
            FullNoteQueue* q = static_cast<FullNoteQueue *>(r->find(qname));
            (*cycle)[i] = q->push_cycles();
            (*rate)[i] = q->push_rate();
            std::cout << qname.c_str() << ", " << (*cycle)[i] << ", " << (*rate)[i] << " | ";
	}
        std::cout << std::endl;
    }
    std::cout << "=========queue output information===========" << std::endl; 
    for(HashMap<String, Vector<String>>::const_iterator it = _task_output.begin();
            it.live(); it++) {
        String task = it.key();
        std::cout << task.c_str() << ": ";
        const Vector<String>& output = it.value();
        Vector<int>* cycle = _task_output_cycle.findp(task);
        Vector<int>* rate = _task_output_rate.findp(task);
        for(int i=0; i<output.size(); ++i) {
            String qname = output[i];
            FullNoteQueue* q = static_cast<FullNoteQueue *>(r->find(qname));
            (*cycle)[i] = q->pull_cycles();
            (*rate)[i] = q->pull_rate();
            std::cout << qname.c_str() << ", " << (*cycle)[i] << ", " << (*rate)[i] << " | ";
        }
	std::cout << std::endl;
    }

    // calculate weight
    Vector<Vector<double>> weight(_id, Vector<double>(_id, 0));
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
            weight[srcid][dstid] = rate[i] / total;
        }
    }
    _weight = weight;

    std::cout << "=========================output weight information=========================" << std::endl;
    for(int i=0; i<_id; ++i) {
        std::cout << _id_task[i].c_str() << ": ";
        for(int j=0; j<_id; ++j) {
            std::cout << "(" << _id_task[j].c_str() << ", " << _weight[i][j] << ")";
        }
        std::cout << std::endl;
    }
}

void
RouterBox::update_traffic(double srctraf) {
    Vector<double> traffic(_id, 0);
    traffic[0] = srctraf;
    for(int i=0; i<_id; i++) {
        for(int j=0; j<_id; j++) {
            traffic[j] += traffic[i] * _weight[i][j];
        }
    }
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

void
RouterBox::add_handlers()
{
    add_read_handler("task_thread", read_handler, H_TASK_THREAD);
    add_read_handler("task_call", read_handler, H_TASK_CALL);
    add_read_handler("task_cost", read_handler, H_TASK_COST);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(RouterBox)
