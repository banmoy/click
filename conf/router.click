// fake-iprouter.click

// This file is a network-independent version of the IP router
// configuration used in our SOSP paper.

// The network sources (FromDevice or PollDevice elements) have been
// replaced with an InfiniteSource, which sends exactly the packets we sent
// in our tests. The ARPQueriers have been replaced with EtherEncaps, and
// the network sinks (ToDevice elements) have been replaced with Discards.
// Thus, you can play around with IP routing -- benchmark our code, for
// example -- even if you don't have the Linux module or the pcap library.


// Kernel configuration for cone as a route between
// 18.26.4 (eth0) and 18.26.7 (eth1).
// Proxy ARPs for 18.26.7 on eth0.

// eth0, 00:00:C0:AE:67:EF, 18.26.4.24
// eth1, 00:00:C0:4F:71:EF, 18.26.7.1

// 0. ARP queries
// 1. ARP replies
// 2. IP
// 3. Other
// We need separate classifiers for each interface because
// we only want proxy ARP on eth0.
c1 :: Classifier(12/0806 20/0001,
                  12/0806 20/0002,
                  12/0800,
                  -);

src :: RatedUdpSource(0:0:0:0:0:0, 18.26.7.29, 1234, 1:1:1:1:1:1, 18.26.7.10, 1234, 0:0:0:0:0:0, 18.26.4.30, 1234, 1:1:1:1:1:1, 18.26.4.10, 1234, RATE 1000000, GUARD 5)

src -> counter_src :: Counter
    -> srcq :: Queue
    -> route :: Unqueue
    -> counter_route :: Counter
    -> [0]c1;
out0 :: Queue(200) -> counter_out0 :: Counter -> dis0 :: Discard;
out1 :: Queue(200) -> counter_out1 :: Counter -> dis1 :: Discard;
tol :: Discard;

// An "ARP querier" for each interface.
fake_arpq0 :: EtherEncap(0x0800, 00:00:c0:ae:67:ef, 00:00:c0:4f:71:ef); //ARPQuerier(18.26.4.24, 00:00:C0:AE:67:EF);
fake_arpq1 :: EtherEncap(0x0800, 00:00:c0:4f:71:ef, 00:00:c0:4f:71:ef); //ARPQuerier(18.26.7.1, 00:00:C0:4F:71:EF);

// Deliver ARP responses to ARP queriers as well as Linux.
t :: Tee(3);
c1[1] -> t;
t[0] -> tol;
t[1] -> fake_arpq0; // was -> [1]arpq0
t[2] -> fake_arpq1; // was -> [1]arpq1

// Connect ARP outputs to the interface queues.
fake_arpq0 -> out0;
fake_arpq1 -> out1;

// Ordinary ARP on eth1.
ar1 :: ARPResponder(18.26.7.1 00:00:C0:4F:71:EF);
c1[0] -> ar1 -> out1;

// IP routing table. Outputs:
// 0: packets for this machine.
// 1: packets for 18.26.4.
// 2: packets for 18.26.7.
// All other packets are sent to output 1, with 18.26.4.1 as the gateway.
rt :: StaticIPLookup(18.26.4.24/32 0,
		    18.26.4.255/32 0,
		    18.26.4.0/32 0,
		    18.26.7.1/32 0,
		    18.26.7.255/32 0,
		    18.26.7.0/32 0,
		    18.26.4.0/24 1,
		    18.26.7.0/24 2,
		    0.0.0.0/0 18.26.4.1 1);

// Hand incoming IP packets to the routing table.
// CheckIPHeader checks all the lengths and length fields
// for sanity.
ip ::   Strip(14)
     -> CheckIPHeader(INTERFACES 18.26.4.1/24 18.26.7.1/24)
     -> [0]rt;
c1[2]-> ip;

// IP packets for this machine.
// ToHost expects ethernet packets, so cook up a fake header.
rt[0] -> EtherEncap(0x0800, 1:1:1:1:1:1, 2:2:2:2:2:2) -> tol;

// These are the main output paths; we've committed to a
// particular output device.
// Check paint to see if a redirect is required.
// Process record route and timestamp IP options.
// Fill in missing ip_src fields.
// Discard packets that arrived over link-level broadcast or multicast.
// Decrement and check the TTL after deciding to forward.
// Fragment.
// Send outgoing packets through ARP to the interfaces.
rt[1] -> fwdq0 :: Queue
      -> fwd0 :: Unqueue
      -> counter_fwd0 :: Counter
      -> DropBroadcasts
      -> gio1 :: IPGWOptions(18.26.4.24)
      -> FixIPSrc(18.26.4.24)
      -> dt1 :: DecIPTTL
      -> fr1 :: IPFragmenter(300)
      -> [0]fake_arpq0;
rt[2] -> fwdq1 :: Queue
      -> fwd1 :: Unqueue
      -> counter_fwd1 :: Counter
      -> DropBroadcasts
      -> gio2 :: IPGWOptions(18.26.7.1)
      -> FixIPSrc(18.26.7.1)
      -> dt2 :: DecIPTTL
      -> fr2 :: IPFragmenter(300)
      -> [0]fake_arpq1;

// DecIPTTL[1] emits packets with expired TTLs.
// Reply with ICMPs. Rate-limit them?
dt1[1] -> ICMPError(18.26.4.24, timeexceeded) -> Discard// [0]rt;
dt2[1] -> ICMPError(18.26.4.24, timeexceeded) -> Discard// [0]rt;

// Send back ICMP UNREACH/NEEDFRAG messages on big packets with DF set.
// This makes path mtu discovery work.
fr1[1] -> ICMPError(18.26.7.1, unreachable, needfrag) -> Discard// [0]rt;
fr2[1] -> ICMPError(18.26.7.1, unreachable, needfrag) -> Discard// [0]rt;

// Send back ICMP Parameter Problem messages for badly formed
// IP options. Should set the code to point to the
// bad byte, but that's too hard.
gio1[1] -> ICMPError(18.26.4.24, parameterproblem) -> [0]rt;
gio2[1] -> ICMPError(18.26.4.24, parameterproblem) -> [0]rt;

// Unknown ethernet type numbers.
c1[3] -> Print(c3) -> Discard;

StaticThreadSched(src 1, route 2, fwd0 3, fwd1 4, dis0 3, dis1 4)
rb::RouterBox(NAME router1, TOPOLOGY "src,,srcq,route,srcq,fwdq0 fwdq1,fwd0,fwdq0,out0,fwd1,fwdq1,out1,dis0,out0,,dis1,out1,")
