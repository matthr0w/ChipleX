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
const sc_time BUS_ARBITRATION_DELAY = BUS_CLK_CYCLE;

// RAM
// -----------------------------------------------
// RAM width in bytes
const unsigned int RAM_WIDTH = 1;
// inverse of clock frequency
const sc_time RAM_CLK_CYCLE = sc_time(1, SC_NS);
// fixed access delay
const sc_time RAM_ACCESS_DELAY = sc_time(20, SC_NS);

// Interconnect
// -----------------------------------------------
// interconnect width in bytes
const unsigned int INTERCONNECT_WIDTH = 1;
// interconnect buffers size
const unsigned int INTERCONNECT_BUFFER_SIZE = 3;
// inverse of clock frequency
const sc_time INTERCONNECT_CLK_CYCLE = sc_time(5, SC_NS);