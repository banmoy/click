#include <click/config.h>
#include <click/args.hh>
#include "routerbox.hh"
#include <click/router.hh>
CLICK_DECLS

RouterBox::RouterBox()
{}

RouterBox::~RouterBox()
{}

int
RouterBox::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_mp("NAME", _router_name)
        .complete() < 0)
        return -1;
    router()->set_router_info(this);
    return 0;
}

String
RouterBox::router_name()
{
    return _router_name;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(RouterBox)