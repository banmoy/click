#include <click/config.h>
#include <click/args.hh>
#include "routerbox.hh"
#include <click/router.hh>
#include "elements/standard/fullnotequeue.hh"
#include <iostream>
CLICK_DECLS

RouterBox::RouterBox()
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
        _task_input.insert(task, Vector<String>());
        _task_input_rate.insert(task, Vector<int>());
        _task_input_cycle.insert(task, Vector<int>());
        _task_output.insert(task, Vector<String>());
        _task_output_rate.insert(task, Vector<int>());
        _task_output_cycle.insert(task, Vector<int>());
        i = ++j;
        while(_topo[j]!=',')
            ++j;
        int k = i;
        while(k<j) {
            while(k<j && _topo[k]!=' ') 
                k++;
            String tmp = _topo.substring(i, k-i).unshared();
            _task_input.findp(task)->push_back(tmp);
            _task_input_rate.findp(task)->push_back(0);
            _task_input_cycle.findp(task)->push_back(0);
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