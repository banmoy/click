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


// src :: FastUDPSource(6000000, -1, 60, 0:0:0:0:0:0, 1.0.0.1, 1234, 1:1:1:1:1:1, 2.0.0.2, 1234)
// src :: RatedSource(DATA \<
//   // Ethernet header
//   00 00 c0 ae 67 ef  00 00 00 00 00 00  08 00
//   // IP header
//   45 00 00 28  00 00 00 00  40 11 77 c3  01 00 00 01  02 00 00 02
//   // UDP header
//   13 69 13 69  00 14 d6 41
//   // UDP payload
//   55 44 50 20  70 61 63 6b  65 74 21 0a  04 00 00 00  01 00 00 00  
//   01 00 00 00  00 00 00 00  00 80 04 08  00 80 04 08  53 53 00 00
//   53 53 00 00  05 00 00 00  00 10 00 00  01 00 00 00  54 53 00 00
//   54 e3 04 08  54 e3 04 08  d8 01 00 00
// >, RATE 200000, LIMIT -1, STOP false, DATA1 \<
//   // Ethernet header
//   00 00 c0 ae 67 ef  00 00 00 00 00 00  08 00
//   // IP header
//   45 00 00 28  00 00 00 00  40 11 77 c3  01 00 00 01  12 1a 07 02
//   // UDP header
//   13 69 13 69  00 14 d6 41
//   // UDP payload
//   55 44 50 20  70 61 63 6b  65 74 21 0a  04 00 00 00  01 00 00 00  
//   01 00 00 00  00 00 00 00  00 80 04 08  00 80 04 08  53 53 00 00
//   53 53 00 00  05 00 00 00  00 10 00 00  01 00 00 00  54 53 00 00
//   54 e3 04 08  54 e3 04 08  d8 01 00 00
// >, GUARD 5)

src :: RatedUdpSource(0:0:0:0:0:0, 18.26.7.29, 1234, 1:1:1:1:1:1, 18.26.7.10, 1234, 0:0:0:0:0:0, 18.26.4.30, 1234, 1:1:1:1:1:1, 18.26.4.10, 1234, RATE 200000, GUARD 5)

src -> counter_src :: Counter
    -> Queue
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
c1[2] -> Paint(2) -> ip;

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
rt[1] -> Queue
      -> fwd0 :: Unqueue
      -> counter_fwd0 :: Counter
      -> DropBroadcasts
      -> cp1 :: PaintTee(1)
      -> gio1 :: IPGWOptions(18.26.4.24)
      -> FixIPSrc(18.26.4.24)
      -> dt1 :: DecIPTTL
      -> fr1 :: IPFragmenter(300)
      -> [0]fake_arpq0;
rt[2] -> Queue
      -> fwd1 :: Unqueue
      -> counter_fwd1 :: Counter
      -> DropBroadcasts
      -> cp2 :: PaintTee(2)
      -> gio2 :: IPGWOptions(18.26.7.1)
      -> FixIPSrc(18.26.7.1)
      -> dt2 :: DecIPTTL
      -> fr2 :: IPFragmenter(300)
      -> [0]fake_arpq1;

// DecIPTTL[1] emits packets with expired TTLs.
// Reply with ICMPs. Rate-limit them?
dt1[1] -> ICMPError(18.26.4.24, timeexceeded) -> [0]rt;
dt2[1] -> ICMPError(18.26.4.24, timeexceeded) -> [0]rt;

// Send back ICMP UNREACH/NEEDFRAG messages on big packets with DF set.
// This makes path mtu discovery work.
fr1[1] -> ICMPError(18.26.7.1, unreachable, needfrag) -> [0]rt;
fr2[1] -> ICMPError(18.26.7.1, unreachable, needfrag) -> [0]rt;

// Send back ICMP Parameter Problem messages for badly formed
// IP options. Should set the code to point to the
// bad byte, but that's too hard.
gio1[1] -> ICMPError(18.26.4.24, parameterproblem) -> [0]rt;
gio2[1] -> ICMPError(18.26.4.24, parameterproblem) -> [0]rt;

// Send back an ICMP redirect if required.
cp1[1] -> ICMPError(18.26.4.24, redirect, host) -> [0]rt;
cp2[1] -> ICMPError(18.26.7.1, redirect, host) -> [0]rt;

// Unknown ethernet type numbers.
c1[3] -> Print(c3) -> Discard;

StaticThreadSched(src 1, route 2, fwd0 3, fwd1 4, dis0 3, dis1 4)
rb::RouterBox(NAME router1)
