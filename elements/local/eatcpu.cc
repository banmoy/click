#include <click/config.h>
#include "eatcpu.hh"
#include <click/error.hh>
#include <click/args.hh>
CLICK_DECLS

EatCpu::EatCpu()
  : _count(0), _loop(0)
{
}

EatCpu::~EatCpu()
{
}

int
EatCpu::configure(Vector<String> &conf, ErrorHandler *errh)
{
  if (Args(conf, this, errh)
    .read_p("LOOP", _loop)
    .complete() < 0)
    return -1;
  if(_loop < 0)
    return errh->error("loop must be non-negative");
  return 0;
}

int
EatCpu::initialize(ErrorHandler *errh)
{
  return 0;
}

Packet *
EatCpu::simple_action(Packet *p)
{
  _count++;
  int i = 0;
  while(i++ < _loop)
    do_something();
  return p;
}

void
EatCpu::do_something() {

}

enum { H_COUNT };

String
EatCpu::read_handler(Element *e, void *thunk)
{
  EatCpu *ec = (EatCpu *)e;
  switch ((intptr_t)thunk) {
    case H_COUNT:
      return String(ec->_count);
    default:
      return "<error>";
  }
}

void
EatCpu::add_handlers()
{
    add_read_handler("count", read_handler, H_COUNT);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(EatCpu)
