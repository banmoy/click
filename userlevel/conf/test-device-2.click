src::FromDevice(ens3, FORCE_IP true)
    -> Queue
//    -> print::IPPrint(OUTFILE /home/ict/lpf/click/userlevel/conf/udp.out)
    -> EtherRewrite(52:54:00:fa:e5:bd, 52:54:00:f2:a0:2b)
    -> c::Counter
    -> sink::ToDevice(ens9)

StaticThreadSched(src 0, sink 1)
rb::RouterBox(NAME udp)
