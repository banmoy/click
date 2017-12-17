// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_EATCPU_HH
#define CLICK_EATCPU_HH
#include <click/element.hh>
CLICK_DECLS

class EatCpu : public Element { public:

    EatCpu();
    ~EatCpu();

    const char *class_name() const      { return "EatCpu"; }
    const char *port_count() const      { return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *);
    int initialize(ErrorHandler *);
    void add_handlers();

    Packet *simple_action(Packet *);

    void do_something();

  private:
    int _count;
    int _loop;

    static String read_handler(Element *, void *);
};

CLICK_ENDDECLS
#endif
