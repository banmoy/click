#include <click/config.h>
#include "eatcpu.hh"
#include <click/error.hh>
#include <click/confparse.hh>
#include <click/args.hh>
#include <cmath>
CLICK_DECLS

EatCpu::EatCpu()
    : _n(0)
{
}

EatCpu::~EatCpu()
{
}

int
EatCpu::configure(Vector<String> &conf, ErrorHandler *errh)
{
   int n = 10;
  if (Args(conf, this, errh)
      .read_p("N", n).complete() < 0)
    return -1;

    _n = n;

  return 0;
}

Packet *
EatCpu::simple_action(Packet *p)
{
  _count = 0;
  for(int i=1; i<=_n; ++i) {
    int s = std::sqrt(i);
    for(int j=1; j<=s; ++j) {
        int k = i / j;
        if(j * k == i) {
            --_count;
            break;
        }
    }
    ++_count;
  }

  return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(EatCpu)