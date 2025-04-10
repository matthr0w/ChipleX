#pragma once

#include "systemc"

#include "Config.h"

using namespace sc_core;

inline sc_time get_bus_arbitration_delay() { return BUS_ARBITRATION_DELAY; }

inline sc_time get_bus_transfer_delay(unsigned int data_size) {
  unsigned int n_cycles = std::ceil(data_size / BUS_WIDTH);
  return n_cycles * BUS_CLK_CYCLE;
}

inline sc_time get_mem_access_delay(unsigned int data_size) {
  unsigned int n_cycles = std::ceil(data_size / RAM_WIDTH);
  sc_time data_delay = n_cycles * RAM_CLK_CYCLE;
  return data_delay + RAM_ACCESS_DELAY;
}