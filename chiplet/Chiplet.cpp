#include "Chiplet.h"

using namespace sc_core;
using namespace tlm;

// TODO:
// + Parameters
// + Delays

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), core1("Core1"), core2("Core2"), ram("RAM"), bus("Bus") {
  initialize();
}

void Chiplet::initialize() {
  // Connect core initiator sockets to bus target sockets
  core1.socket.bind(bus.target_socket_core1);
  core2.socket.bind(bus.target_socket_core2);

  // Connect bus initiator socket to RAM target socket
  bus.initiator_socket.bind(ram.socket);
}