#ifndef CLICK_RATEDUDPSOURCE_HH
#define CLICK_RATEDUDPSOURCE_HH
#include <click/element.hh>
#include <click/tokenbucket.hh>
#include <click/task.hh>
#include <clicknet/ether.h>
#include <clicknet/udp.h>
CLICK_DECLS

/*
=c

RatedUdpSource([DATA, RATE, LIMIT, ACTIVE, I<KEYWORDS>])

=s basicsources

generates packets at specified rate

=d

Creates packets consisting of DATA, emitting at most LIMIT such packets out
its single output at a rate of RATE packets per second. When used as a push
element, RatedUdpSource will send a maximum of one packet per scheduling, so very
high RATEs may not be achievable. If LIMIT is negative, sends packets forever.
Will send packets only if ACTIVE is true. Default DATA is at least 64 bytes
long. Default RATE is 10. Default LIMIT is -1 (send packets forever). Default
ACTIVE is true.

Keyword arguments are:

=over 8

=item DATA

String. Same as the DATA argument.

=item LENGTH

Integer. If set, the outgoing packet will have this length.

=item RATE

Integer. Same as the RATE argument.

=item BANDWIDTH

Integer. Sets the RATE argument based on the initial outgoing packet length
and a target bandwdith.

=item LIMIT

Integer. Same as the LIMIT argument.

=item ACTIVE

Boolean. Same as the ACTIVE? argument.

=item STOP

Boolean. If true, then stop the driver once LIMIT packets are sent. Default is
false.

=back

To generate a particular repeatable traffic pattern, use this element's
B<rate> and B<active> handlers in conjunction with Script.

=e

  RatedUdpSource(\<0800>, 10, 1000) -> Queue -> ...

=h count read-only
Returns the total number of packets that have been generated.
=h reset write-only
Resets the number of generated packets to 0. The RatedUdpSource will then
generate another LIMIT packets (if it is active).
=h data read/write
Returns or sets the DATA parameter.
=h length read/write
Returns or sets the LENGTH parameter.
=h rate read/write
Returns or sets the RATE parameter.
=h limit read/write
Returns or sets the LIMIT parameter. Negative numbers mean no limit.
=h active read/write
Makes the element active or inactive.

=a

InfiniteSource, Script */

class RatedUdpSource : public Element { public:

    RatedUdpSource() CLICK_COLD;

    const char *class_name() const		{ return "RatedUdpSource"; }
    const char *port_count() const		{ return PORTS_0_1; }
    void add_handlers() CLICK_COLD;

    int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    bool run_task(Task *task);
    Packet *pull(int);

  protected:

    static const unsigned NO_LIMIT = 0xFFFFFFFFU;

    int _pktnum;
    unsigned _len;

    Packet *_packet;
    click_ether _ethh;
    struct in_addr _sipaddr;
    struct in_addr _dipaddr;
    uint16_t _sport;
    uint16_t _dport;

    Packet *_packet1;
    click_ether _ethh1;
    struct in_addr _sipaddr1;
    struct in_addr _dipaddr1;
    uint16_t _sport1;
    uint16_t _dport1;


    TokenBucket _tb;
    unsigned _count;
    unsigned _limit;
    bool _active;
    bool _stop;
    Task _task;
    Timer _timer;
    int _guard;

    void setup_packet();

    void setup_packet1();

    Packet *get_packet();

    static int change_param(const String &, Element *, void *, ErrorHandler *);

};

CLICK_ENDDECLS
#endif
