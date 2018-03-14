#ifndef CLICK_ROUTERINFO_H
#define CLICK_ROUTERINFO_H

#include <click/string.hh>

class RouterInfo {
public:
    RouterInfo() {}
    ~RouterInfo() {}

    virtual String router_name() = 0;

    virtual void update_topology() {}

    virtual void update_traffic(double srctraf) {}
};

#endif