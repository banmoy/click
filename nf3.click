i::InfiniteSource(DATA \<00 00 c0 ae 67 ef  00 00 00 00 00 00  08 00
45 00 00 28  00 00 00 00  40 11 77 c3  01 00 00 01
02 00 00 02  13 69 13 69  00 14 d6 41  55 44 50 20
70 61 63 6b  65 74 21 0a>, LIMIT -1, STOP false)
	-> c::Counter
	-> Strip(14)
	-> Align(4, 0)    // in case we're not on x86
	-> CheckIPHeader(BADSRC 18.26.4.255 2.255.255.255 1.255.255.255)
	-> ss::StrideSwitch(1, 2)

ss[0] -> Queue
      -> c0::Counter
	  -> d0::Discard;

ss[1] -> Queue
	  -> c1::Counter
 	  -> d1::Discard

StaticThreadSched(i 1, d0 2, d1 3)
rb::RouterBox(NAME nf3)
