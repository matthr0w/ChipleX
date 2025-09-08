#pragma once

#include <systemc>
#include <tlm>

#include "modules/AXIBus.h"
#include "modules/Core.h"

struct InterconnectBase {
  unsigned num_interconnects;

  simple_target_socket_tagged<InterconnectBase> **in_ports;
  simple_initiator_socket_tagged<InterconnectBase> **out_ports;

  InterconnectBase(unsigned num_interconnects)
      : num_interconnects(num_interconnects) {
    in_ports =
        new simple_target_socket_tagged<InterconnectBase> *[num_interconnects];
    out_ports = new simple_initiator_socket_tagged<InterconnectBase>
        *[num_interconnects];
  }

  virtual ~InterconnectBase() {
    delete[] in_ports;
    delete[] out_ports;
  }

  virtual void bind_axi(AXIBus &bus, sc_clock &clk) = 0;
  virtual void bind_core(unsigned index, Core &core) = 0;
};