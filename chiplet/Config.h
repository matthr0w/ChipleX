#pragma once

#include "systemc"

using namespace sc_core;

// Bus
// -----------------------------------------------
// bus width in bytes
const unsigned int BUS_WIDTH = 4;
// inverse of clock frequency
const sc_time BUS_CLK_CYCLE = sc_time(5, SC_NS);
// fixed arbitration delay
const sc_time BUS_ARBITRATION_DELAY = sc_time(10, SC_NS);

// RAM
// -----------------------------------------------
// RAM width in bytes
const unsigned int RAM_WIDTH = 1;
// inverse of clock frequency
const sc_time RAM_CLK_CYCLE = sc_time(1, SC_NS);
// fixed access delay
const sc_time RAM_ACCESS_DELAY = sc_time(20, SC_NS);