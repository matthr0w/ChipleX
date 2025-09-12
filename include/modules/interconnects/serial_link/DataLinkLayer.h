#pragma once

#include <systemc>
#include <tlm>

#include "modules/interconnects/serial_link/FifoIf.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(SLDataLinkLayer) {
public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;
  sc_in<bool> rst_n;

  // -------------------------------------------------------
  // Ports
  // -------------------------------------------------------
  sc_port<FifoIf> stream_fifo_in;

  SLDataLinkLayer(sc_module_name name, unsigned chip_id);

private:
  void clk_posedge();

  // -------------------------------------------------------
  // Parameters
  // -------------------------------------------------------
  const unsigned chip_id;
};