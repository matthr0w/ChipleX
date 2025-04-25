#include "Chiplet.h"

using namespace sc_core;
using namespace tlm;

unsigned int Chiplet::instance = 0;

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      core0("Core0", chiplet_id, 0), core1("Core1", chiplet_id, 1), ram("RAM"),
      bus("Bus", chiplet_id),
      interconnectprotocol("InterconnectProtocol", chiplet_id),
      interconnect0("Interconnect0", chiplet_id),
      interconnect1("Interconnect1", chiplet_id) {
  initialize();
}

void Chiplet::initialize() {
  // sockets
  // cores
  core0.socket.bind(bus.core0_target_socket);
  core1.socket.bind(bus.core1_target_socket);
  // RAM
  bus.ram_initiator_socket.bind(ram.socket);
  // interconnects
  bus.interconnect_initiator_socket.bind(
      interconnectprotocol.bus_target_socket);
  interconnectprotocol.bus_initiator_socket.bind(
      bus.interconnect_target_socket);
  interconnectprotocol.interconnect0_initiator_socket.bind(
      interconnect0.protocol_target_socket);
  interconnect0.protocol_initiator_socket.bind(
      interconnectprotocol.interconnect0_target_socket);
  interconnectprotocol.interconnect1_initiator_socket.bind(
      interconnect1.protocol_target_socket);
  interconnect1.protocol_initiator_socket.bind(
      interconnectprotocol.interconnect1_target_socket);

  // interrupt sockets
  interconnectprotocol.core0_irq_initiator_socket.bind(core0.irq_socket);
  interconnectprotocol.core1_irq_initiator_socket.bind(core1.irq_socket);
}