// 203 machine
define($SDEV ens2f3, $DDEV ens2f2)
define($SMAC 68:05:ca:3a:1a:32, $DMAC 68:05:ca:2d:5b:e9)

// src device from 201
sdev :: FromDevice($SDEV, SNIFFER false);

// dest device to 202
ddev :: ToDevice($DDEV)

// classify ip packets
cf :: Classifier(12/0800, -);

// discard
discard :: Counter -> Discard;

// static loopup table
rt :: StaticIPLookup(
        192.168.4.203/32 0, // 203 machine
        192.168.5.203/32 0,  // 203 machine
        192.168.5.202/32 1, // 202 machine
        192.168.3.202/32 1, // 202 machine
        0.0.0.0/0 2);       // unknown packets

ip :: Strip(14)
     -> CheckIPHeader(INTERFACES 192.168.4.202/24)
     -> [0]rt;

sdev -> cf[0] -> ip
cf[1] -> discard
rt[2] -> discard

// to 203
rt[0] -> DropBroadcasts
      -> dt1 :: DecIPTTL
      -> EtherEncap(0x0800, $SMAC, $DMAC)
      -> Queue
      -> ddev;

// host
rt[1] -> discard;