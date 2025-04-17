#pragma once

#include "Config.h"

#include "systemc"

#include "include/logging.h"

using namespace sc_core;

inline sc_time get_bus_arbitration_delay() {
  SC_LOG_DELAY("Bus Arbitration", BUS_ARBITRATION_DELAY);
  return BUS_ARBITRATION_DELAY;
}

inline sc_time get_bus_transfer_delay(unsigned int data_size) {
  unsigned int cycles = (data_size + BUS_WIDTH - 1) / BUS_WIDTH;
  SC_LOG_DELAY("Bus Transfer", cycles * BUS_CLK_CYCLE);
  return cycles * BUS_CLK_CYCLE;
}

inline sc_time get_mem_access_delay(unsigned int data_size) {
  unsigned int cycles = (data_size + RAM_WIDTH - 1) / RAM_WIDTH;
  sc_time data_delay = cycles * RAM_CLK_CYCLE;
  SC_LOG_DELAY("RAM Access", data_delay + RAM_ACCESS_DELAY);
  return data_delay + RAM_ACCESS_DELAY;
}

inline sc_time get_interconnect_transfer_delay(unsigned int data_size) {
  unsigned int cycles =
      (data_size + INTERCONNECT_WIDTH - 1) / INTERCONNECT_WIDTH;
  SC_LOG_DELAY("Interconnect Transfer", cycles * INTERCONNECT_CLK_CYCLE);
  return cycles * INTERCONNECT_CLK_CYCLE;
}