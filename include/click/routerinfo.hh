#ifndef CLICK_ROUTERINFO_H
#define CLICK_ROUTERINFO_H

#include <click/task.hh>
#include <click/string.hh>
#include <click/vector.hh>

class RouterInfo {
public:
    RouterInfo() {}
    ~RouterInfo() {}

    virtual String router_name() = 0;

    virtual void update_info() = 0;
	
    virtual double src_rate() = 0;

    virtual Vector<Task*>& task() = 0;

    virtual Vector<double>& task_rate(double refrate) = 0;

    virtual Vector<int>& task_cycle() = 0;

    virtual void check_congestion() = 0;

    virtual void update_chain(bool move) = 0;

    virtual void update_local_chain(bool move) = 0;

    virtual bool update_coco_chain(bool move)=0; //add defination of update_coco_chain

    virtual void reset_element(String name) = 0;
};

#endif
