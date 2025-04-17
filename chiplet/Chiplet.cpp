#include "Chiplet.h"

using namespace sc_core;
using namespace tlm;

unsigned int Chiplet::instance = 0;

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      core0("Core0", chiplet_id, 0), core1("Core1", chiplet_id, 1), ram("RAM"),
      bus("Bus", chiplet_id), interconnect("Interconnect", chiplet_id) {
  initialize();
}

void Chiplet::initialize() {
  // sockets
  // cores
  core0.socket.bind(bus.core0_target_socket);
  core1.socket.bind(bus.core1_target_socket);
  // RAM
  bus.ram_initiator_socket.bind(ram.socket);
  // interconnect
  bus.interconnect_initiator_socket.bind(interconnect.bus_target_socket);
  interconnect.bus_initiator_socket.bind(bus.interconnect_target_socket);

  // interrupt sockets
  interconnect.core0_irq_initiator_socket.bind(core0.irq_socket);
  interconnect.core1_irq_initiator_socket.bind(core1.irq_socket);
}