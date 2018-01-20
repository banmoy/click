#ifndef CLICK_ROUTERBOX_H
#define CLICK_ROUTERBOX_H

#include <click/element.hh>
#include <click/routerinfo.hh>

class RouterBox : public Element, public RouterInfo {
public:
    RouterBox() CLICK_COLD;
    ~RouterBox() CLICK_COLD;

    const char *class_name() const { return "RouterBox"; }

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;

    String router_name();

    void add_handlers() CLICK_COLD;

private:
    String _router_name;

    static String read_handler(Element*, void*) CLICK_COLD;
};

#endif