#pragma once

#include <systemc>
#include <tlm>

#include "modules/interconnects/serial_link/Types.h"

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
  sc_fifo_in<Payload_t *> stream_fifo_in;

  SLDataLinkLayer(sc_module_name name);

private:
  void clk_posedge();
};