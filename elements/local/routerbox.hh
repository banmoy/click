#ifndef CLICK_ROUTERBOX_H
#define CLICK_ROUTERBOX_H

#include <click/element.hh>
#include <click/routerinfo.hh>
#include <click/hashmap.hh>
#include <click/string.hh>

class RouterBox : public Element, public RouterInfo {
public:
    RouterBox() CLICK_COLD;
    ~RouterBox() CLICK_COLD;

    const char *class_name() const { return "RouterBox"; }

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;

    String router_name();

    void update_topology();

    void add_handlers() CLICK_COLD;

private:
    String _router_name;
    String _topo;

    HashMap<String, Vector<String>> _task_input;

    HashMap<String, Vector<int>> _task_input_rate;

    HashMap<String, Vector<int>> _task_input_cycle;

    HashMap<String, Vector<String>> _task_output;

    HashMap<String, Vector<int>> _task_output_rate;

    HashMap<String, Vector<int>> _task_output_cycle;

    void setup_topology();

    static String read_handler(Element*, void*) CLICK_COLD;
};

#endif