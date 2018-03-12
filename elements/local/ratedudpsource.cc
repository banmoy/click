/*
 * ratedsource.{cc,hh} -- generates configurable rated stream of packets.
 * Benjie Chen, Eddie Kohler (based on udpgen.o)
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2008 Regents of the University of California
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <clicknet/ip.h>
#include "ratedudpsource.hh"
#include <click/args.hh>
#include <click/etheraddress.hh>
#include <click/standard/alignmentinfo.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/straccum.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/glue.hh>
#include <cstdlib>
#include <ctime>
CLICK_DECLS

const unsigned RatedSource::NO_LIMIT;

RatedSource::RatedSource()
  : _packet(0), _packet1(0), _task(this), _timer(&_task)
{
}

int
RatedSource::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int pktnum = 1;
    int len = 60;
    unsigned rate = 10;
    int limit = -1;
    bool active = true, stop = false;
    int guard = 10;

    Args eleargs(conf, this, errh);

    eleargs.read_mp("PKTNUM", pktnum)
           .read_mp("SRCETH", EtherAddressArg(), _ethh.ether_shost)
           .read_mp("SRCIP", _sipaddr)
           .read_mp("SPORT", IPPortArg(IP_PROTO_UDP), _sport)
           .read_mp("DSTETH", EtherAddressArg(), _ethh.ether_dhost)
           .read_mp("DSTIP", _dipaddr)
           .read_mp("DPORT", IPPortArg(IP_PROTO_UDP), _dport)
           .read("RATE", rate)
	         .read("LIMIT", limit)
	         .read("ACTIVE", active)
	         .read("STOP", stop)
           .read("GUARD", guard);

    
    if(pktnum > 1) {
        eleargs.read_mp("SRCETH1", EtherAddressArg(), _ethh1.ether_shost)
           .read_mp("SRCIP1", _sipaddr1)
           .read_mp("SPORT1", IPPortArg(IP_PROTO_UDP), _sport1)
           .read_mp("DSTETH1", EtherAddressArg(), _ethh1.ether_dhost)
           .read_mp("DSTIP1", _dipaddr1)
           .read_mp("DPORT1", IPPortArg(IP_PROTO_UDP), _dport1)
    }

    if(eleargs.complete() < 0)
	     return -1;


    _pktnum = pktnum;
    _len = len<60 ? 60 : len;
    _tb.assign(rate, (rate < 200 ? 2 : rate / 100));
    _limit = (limit >= 0 ? unsigned(limit) : NO_LIMIT);
    _active = active;
    _stop = stop;
    _guard = guard;

    return 0;
}

int
RatedSource::initialize(ErrorHandler *errh)
{
    _count = 0;
    if (output_is_push(0))
	ScheduleInfo::initialize_task(this, &_task, errh);
    _tb.set(1);
    _timer.initialize(this);

    setup_packet();
    if(_pktnum > 1)
      setup_packet1();

    srand (time(NULL));

    return 0;
}

void
RatedSource::cleanup(CleanupStage)
{
    if (_packet)
	_packet->kill();
    if(_packet1)
      _packet1->kill();
    _packet = 0;
    _packet1 = 0;
}

bool
RatedSource::run_task(Task *)
{
    if (!_active)
	return false;
    if (_limit != NO_LIMIT && _count >= _limit) {
	if (_stop)
	    router()->please_stop_driver();
	return false;
    }

    _tb.refill();
    if (_tb.remove_if(1)) {
	Packet *p = get_packet()->clone();
	p->set_timestamp_anno(Timestamp::now());
	output(0).push(p);
	_count++;
	_task.fast_reschedule();
	return true;
    } else {
	_timer.schedule_after(Timestamp::make_jiffies(_tb.time_until_contains(1)));
	return false;
    }
}

Packet *
RatedSource::pull(int)
{
    if (!_active)
	return 0;
    if (_limit != NO_LIMIT && _count >= _limit) {
	if (_stop)
	    router()->please_stop_driver();
	return 0;
    }

    _tb.refill();
    if (_tb.remove_if(1)) {
	_count++;
	Packet *p = get_packet()->clone();
	p->set_timestamp_anno(Timestamp::now());
	return p;
    } else
	return 0;
}

Packet*
RatedSource::get_packet() {
  return _pktnum == 1 || (rand()%10)<_guard ? _packet : _packet1;
}

void
RatedSource::setup_packet()
{
    _ethh.ether_type = htons(0x0800);
    WritablePacket *q = Packet::make(_len);
    _packet = q;
    memcpy(q->data(), &_ethh, 14);
    click_ip *ip = reinterpret_cast<click_ip *>(q->data()+14);
    click_udp *udp = reinterpret_cast<click_udp *>(ip + 1);

    // set up IP header
    ip->ip_v = 4;
    ip->ip_hl = sizeof(click_ip) >> 2;
    ip->ip_len = htons(_len-14);
    ip->ip_id = 0;
    ip->ip_p = IP_PROTO_UDP;
    ip->ip_src = _sipaddr;
    ip->ip_dst = _dipaddr;
    ip->ip_tos = 0;
    ip->ip_off = 0;
    ip->ip_ttl = 250;
    ip->ip_sum = 0;
    ip->ip_sum = click_in_cksum((unsigned char *)ip, sizeof(click_ip));
    _packet->set_dst_ip_anno(IPAddress(_dipaddr));
    _packet->set_ip_header(ip, sizeof(click_ip));

    // set up UDP header
    udp->uh_sport = htons(_sport);
    udp->uh_dport = htons(_dport);
    udp->uh_sum = 0;
    unsigned short len = _len-14-sizeof(click_ip);
    udp->uh_ulen = htons(len);
    unsigned csum = click_in_cksum((uint8_t *)udp, len);
    udp->uh_sum = click_in_cksum_pseudohdr(csum, ip, len);
}

void
RatedSource::setup_packet1()
{
    _ethh1.ether_type = htons(0x0800);
    WritablePacket *q = Packet::make(_len);
    _packet1 = q;
    memcpy(q->data(), &_ethh1, 14);
    click_ip *ip = reinterpret_cast<click_ip *>(q->data()+14);
    click_udp *udp = reinterpret_cast<click_udp *>(ip + 1);

    // set up IP header
    ip->ip_v = 4;
    ip->ip_hl = sizeof(click_ip) >> 2;
    ip->ip_len = htons(_len-14);
    ip->ip_id = 0;
    ip->ip_p = IP_PROTO_UDP;
    ip->ip_src = _sipaddr1;
    ip->ip_dst = _dipaddr1;
    ip->ip_tos = 0;
    ip->ip_off = 0;
    ip->ip_ttl = 250;
    ip->ip_sum = 0;
    ip->ip_sum = click_in_cksum((unsigned char *)ip, sizeof(click_ip));
    _packet1->set_dst_ip_anno(IPAddress(_dipaddr1));
    _packet1->set_ip_header(ip, sizeof(click_ip));

    // set up UDP header
    udp->uh_sport = htons(_sport1);
    udp->uh_dport = htons(_dport1);
    udp->uh_sum = 0;
    unsigned short len = _len-14-sizeof(click_ip);
    udp->uh_ulen = htons(len);
    unsigned csum = click_in_cksum((uint8_t *)udp, len);
    udp->uh_sum = click_in_cksum_pseudohdr(csum, ip, len);
}


String
RatedSource::read_param(Element *e, void *vparam)
{
  RatedSource *rs = (RatedSource *)e;
  switch ((intptr_t)vparam) {
   case 0:			// data
    return rs->_data;
   case 1:			// rate
    return String(rs->_tb.rate());
   case 2:			// limit
    return (rs->_limit != NO_LIMIT ? String(rs->_limit) : String("-1"));
   default:
    return "";
  }
}

int
RatedSource::change_param(const String &s, Element *e, void *vparam,
			  ErrorHandler *errh)
{
  RatedSource *rs = (RatedSource *)e;
  switch ((intptr_t)vparam) {

  case 0:			// data
      rs->_data = s;
      if (rs->_packet)
	  rs->_packet->kill();
      rs->_packet = Packet::make(rs->_data.data(), rs->_data.length());
      break;

  case 1: {			// rate
      unsigned rate;
      if (!IntArg().parse(s, rate))
	  return errh->error("syntax error");
      rs->_tb.assign_adjust(rate, rate < 200 ? 2 : rate / 100);
      break;
  }

   case 2: {			// limit
     int limit;
     if (!IntArg().parse(s, limit))
       return errh->error("syntax error");
     rs->_limit = (limit >= 0 ? unsigned(limit) : NO_LIMIT);
     break;
   }

  case 3: {			// active
      bool active;
      if (!BoolArg().parse(s, active))
	  return errh->error("syntax error");
      rs->_active = active;
      if (rs->output_is_push(0) && !rs->_task.scheduled() && active) {
	  rs->_tb.set(1);
	  rs->_task.reschedule();
      }
      break;
  }

  case 5: {			// reset
      rs->_count = 0;
      rs->_tb.set(1);
      if (rs->output_is_push(0) && !rs->_task.scheduled() && rs->_active)
	  rs->_task.reschedule();
      break;
  }

  case 6: {			// datasize
      int datasize;
      if (!IntArg().parse(s, datasize))
	  return errh->error("syntax error");
      rs->_datasize = datasize;
      rs->setup_packet();
      break;
  }
  }
  return 0;
}

void
RatedSource::add_handlers()
{
  add_read_handler("data", read_param, 0, Handler::f_calm);
  add_write_handler("data", change_param, 0, Handler::f_raw);
  add_read_handler("rate", read_param, 1);
  add_write_handler("rate", change_param, 1);
  add_read_handler("limit", read_param, 2, Handler::f_calm);
  add_write_handler("limit", change_param, 2);
  add_data_handlers("active", Handler::f_read | Handler::f_checkbox, &_active);
  add_write_handler("active", change_param, 3);
  add_data_handlers("count", Handler::f_read, &_count);
  add_write_handler("reset", change_param, 5, Handler::f_button);
  add_data_handlers("length", Handler::f_read, &_datasize);
  add_write_handler("length", change_param, 6);
  // deprecated
  add_data_handlers("datasize", Handler::f_read | Handler::f_deprecated, &_datasize);
  add_write_handler("datasize", change_param, 6);

  if (output_is_push(0))
    add_task_handlers(&_task);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(RatedSource)
