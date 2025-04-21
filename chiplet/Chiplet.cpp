#include "Chiplet.h"

using namespace sc_core;
using namespace tlm;

unsigned int Chiplet::instance = 0;

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      core0("Core0", chiplet_id, 0), core1("Core1", chiplet_id, 1), ram("RAM"),
      bus("Bus", chiplet_id), interconnect0("Interconnect0", chiplet_id),
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
  bus.interconnect0_initiator_socket.bind(interconnect0.bus_target_socket);
  interconnect0.bus_initiator_socket.bind(bus.interconnect0_target_socket);
  bus.interconnect1_initiator_socket.bind(interconnect1.bus_target_socket);
  interconnect1.bus_initiator_socket.bind(bus.interconnect1_target_socket);

  // interrupt sockets
  interconnect0.core0_irq_initiator_socket.bind(core0.irq0_socket);
  interconnect0.core1_irq_initiator_socket.bind(core1.irq0_socket);
  interconnect1.core0_irq_initiator_socket.bind(core0.irq1_socket);
  interconnect1.core1_irq_initiator_socket.bind(core1.irq1_socket);
}