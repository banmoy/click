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

    void update_traffic(double srctraf);

    void add_handlers() CLICK_COLD;

private:
    String _router_name;
    String _topo;
    String _src_task;
    int _id;

    HashMap<String, int> _task_id;

    HashMap<int, String> _id_task;

    Vector<Vector<int>> _adj_table;

    Vector<int> _toposort_task;

    Vector<Vector<double>> _weight;

    Vector<double> _traffic;

    HashMap<String, Vector<String>> _task_input;

    HashMap<String, Vector<int>> _task_input_rate;

    HashMap<String, Vector<int>> _task_input_cycle;

    HashMap<String, Vector<String>> _task_output;

    HashMap<String, Vector<int>> _task_output_rate;

    HashMap<String, Vector<int>> _task_output_cycle;

    HashMap<String, String> _input_to_task;

    void setup_topology();

    void topology_sort();

    static String read_handler(Element*, void*) CLICK_COLD;
};

#endif