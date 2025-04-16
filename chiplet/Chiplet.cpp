#include "Chiplet.h"

using namespace sc_core;
using namespace tlm;

unsigned int Chiplet::instance = 0;

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      core0("Core0", chiplet_id, 0), core1("Core1", chiplet_id, 1),
      ram("RAM"), bus("Bus", chiplet_id) {
  initialize();
}

void Chiplet::initialize() {
  // connect core initiator sockets to bus target sockets
  core0.socket.bind(bus.core0_target_socket);
  core1.socket.bind(bus.core1_target_socket);

  // connect bus initiator socket to RAM target socket
  bus.ram_initiator_socket.bind(ram.socket);
}