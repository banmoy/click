
src::FromDevice(ens9, FORCE_IP true)
    -> Queue
//    -> print::IPPrint(OUTFILE /home/ict/lpf/click/userlevel/conf/udp.out)
    -> c::Counter
    -> d::Discard

StaticThreadSched(src 0, d 1)
rb::RouterBox(NAME udp)
