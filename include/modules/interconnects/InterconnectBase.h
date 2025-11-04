#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "ARM/TLM/arm_axi4.h"
#include "modules/chiplets/Clocks.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

struct InterconnectBase {
  ARM::AXI::SimpleTargetSocket<InterconnectBase> *axi_in_port;
  ARM::AXI::SimpleInitiatorSocket<InterconnectBase> *axi_out_port;

  simple_target_socket_tagged<InterconnectBase> **link_in_ports;
  simple_initiator_socket_tagged<InterconnectBase> **link_out_ports;

  simple_initiator_socket_tagged<InterconnectBase> **irq_ports;

  InterconnectBase(unsigned num_cores, unsigned num_links) {
    link_in_ports =
        new simple_target_socket_tagged<InterconnectBase> *[num_links];
    link_out_ports =
        new simple_initiator_socket_tagged<InterconnectBase> *[num_links];
    irq_ports =
        new simple_initiator_socket_tagged<InterconnectBase> *[num_cores];
  }

  virtual ~InterconnectBase() {
    delete[] link_in_ports;
    delete[] link_out_ports;
    delete[] irq_ports;
  }

  virtual void bind_clocks(Clocks &clocks) = 0;
};