#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "ARM/TLM/arm_axi4.h"
#include "modules/chiplets/Clocks.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

struct InterconnectBase {
  // Chiplet
  const unsigned chiplet_id;
  const ChipletConfig chiplet_config;

  // Interconnect
  const unsigned interconnect_id;
  const InterconnectType interconnect_type;
  const InterconnectConfig interconnect_config;

  // Connections
  const std::vector<ConnectionConfig> connections;

  // Parameters
  const unsigned num_links;
  const unsigned num_cores;
  const unsigned axi_width;

  // AXI ports
  ARM::AXI::SimpleTargetSocket<InterconnectBase> *axi_in_port;
  ARM::AXI::SimpleInitiatorSocket<InterconnectBase> *axi_out_port;

  // Link ports
  simple_target_socket_tagged<InterconnectBase> **link_in_ports;
  simple_initiator_socket_tagged<InterconnectBase> **link_out_ports;

  // IRQ ports
  simple_initiator_socket_tagged<InterconnectBase> **irq_ports;

  InterconnectBase(unsigned chiplet_id, ChipletConfig chiplet_config,
                   unsigned interconnect_id,
                   InterconnectConfig interconnect_config)
      : chiplet_id(chiplet_id), chiplet_config(chiplet_config),
        interconnect_id(interconnect_id),
        interconnect_type(interconnect_config.type),
        interconnect_config(interconnect_config),
        connections(interconnect_config.connections),
        num_links(interconnect_config.connections.size()),
        num_cores(chiplet_config.node["cores"]["num"].as<unsigned>()),
        axi_width(chiplet_config.node["axi"]["width"].as<unsigned>()) {
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