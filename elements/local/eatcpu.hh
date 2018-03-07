#ifndef CLICK_EATCPU_H
#define CLICK_EATCPU_H

#include <click/element.hh>
CLICK_DECLS

class EatCpu : public Element {
public:
    EatCpu() CLICK_COLD;
    ~EatCpu() CLICK_COLD;

    const char *class_name() const { return "EatCpu"; }
    const char *port_count() const      { return PORTS_1_1; }

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;
    // void add_handlers() CLICK_COLD;

    Packet *simple_action(Packet *);

private:
    int _n;
    int _count;
};

CLICK_ENDDECLS
#endif