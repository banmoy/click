#ifndef CLICK_ROUTERINFO_H
#define CLICK_ROUTERINFO_H

#include <string.hh>

class RouterInfo {
public:
    RouterInfo() {}
    ~RouterInfo() {}

    virtual String router_name() = 0;
};

#endif