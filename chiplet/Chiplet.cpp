#include "Chiplet.h"

using namespace sc_core;
using namespace tlm;

unsigned int Chiplet::total_instances = 0;
unsigned int Chiplet::instance = 0;

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), id(Chiplet::instance++),
      core1("Core1", total_instances, id), core2("Core2", total_instances, id),
      ram("RAM"), bus("Bus", id) {
  initialize();
}

void Chiplet::initialize() {
  // connect core initiator sockets to bus target sockets
  core1.socket.bind(bus.target_socket_core1);
  core2.socket.bind(bus.target_socket_core2);

  // connect bus initiator socket to RAM target socket
  bus.ram_initiator_socket.bind(ram.socket);
}