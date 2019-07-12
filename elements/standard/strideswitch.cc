/*
 * stridesched.{cc,hh} -- stride-scheduling packet scheduler
 * Max Poletto, Eddie Kohler
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Mazu Networks, Inc.
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
#include "strideswitch.hh"
#include <click/confparse.hh>
#include <click/error.hh>
CLICK_DECLS

StrideSwitch::StrideSwitch()
{
}

void
StrideSwitch::push(int, Packet *p)
{
	_packet_count++;
	if (_port_1_enabled && _packet_count % 2 == 0) {
		output(1).push(p);
	} else {
		output(0).push(p);
	}
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(StrideSched)
EXPORT_ELEMENT(StrideSwitch)
