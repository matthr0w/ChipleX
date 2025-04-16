#include "chiplet/Chiplet.h"
#include "interconnect/Interconnect.h"

#include <systemc>

using namespace sc_core;
using namespace tlm;

int sc_main(int argc, char *argv[]) {
  Chiplet chiplet0("Chiplet0");
  Chiplet chiplet1("Chiplet1");
  Interconnect interconnect("Interconnect");

  chiplet0.bus.interconnect_initiator_socket.bind(interconnect.socket_A_in);
  chiplet1.bus.interconnect_initiator_socket.bind(interconnect.socket_B_in);
  interconnect.socket_A_out.bind(chiplet0.bus.interconnect_target_socket);
  interconnect.socket_B_out.bind(chiplet1.bus.interconnect_target_socket);

  sc_start(500, SC_NS);

  return 0;
}