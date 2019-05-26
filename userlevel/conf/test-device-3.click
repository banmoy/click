src::FromDevice(ens3, FORCE_IP true)
    -> Queue
//    -> print::IPPrint(OUTFILE /home/ict/lpf/click/userlevel/conf/udp.out)
    -> EtherRewrite(52:54:00:67:7d:d1, 52:54:00:09:4d:73)
    -> c::Counter
    -> sink::ToDevice(ens9)

StaticThreadSched(src 0, sink 1)
rb::RouterBox(NAME udp)
