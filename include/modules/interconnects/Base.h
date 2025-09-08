#pragma once

#include <systemc>
#include <tlm>

#include "modules/AXIBus.h"
#include "modules/Core.h"

struct InterconnectBase {
  unsigned num_interconnects;

  simple_target_socket_tagged<InterconnectBase> **tsockets;
  simple_initiator_socket_tagged<InterconnectBase> **isockets;

  InterconnectBase(unsigned num_interconnects)
      : num_interconnects(num_interconnects) {
    tsockets =
        new simple_target_socket_tagged<InterconnectBase> *[num_interconnects];
    isockets = new simple_initiator_socket_tagged<InterconnectBase>
        *[num_interconnects];
  }

  virtual ~InterconnectBase() {
    delete[] tsockets;
    delete[] isockets;
  }

  virtual void bind_axi(AXIBus &bus, sc_clock &clk) = 0;
  virtual void bind_core(unsigned index, Core &core) = 0;
};