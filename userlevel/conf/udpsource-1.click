udpsrc::FastUDPSource(500000, -1, 60, 52:54:00:c8:f4:b2, 192.168.122.9, 1234,
                                    52:54:00:c5:63:6a, 192.168.122.173, 1234, 1, 0, 1)
  -> c::Counter
  -> ToDevice(ens3);

StaticThreadSched(udpsrc 1)
rb::RouterBox(NAME udpsource)