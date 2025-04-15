#include "chiplet/Chiplet.h"
#include "interconnect/Interconnect.h"

#include <systemc>

using namespace sc_core;
using namespace tlm;

// TODO //
// chiplet:
// + memory size & space of RAMs
// interconnect:
// + backward pathes
// + payload extension

// FIXME //
// + bus arbitration with interconnect

int sc_main(int argc, char *argv[]) {
  Chiplet::total_instances = 2;

  Chiplet chiplet1("Chiplet1");
  Chiplet chiplet2("Chiplet2");
  Interconnect interconnect("Interconnect");

  chiplet1.bus.interconnect_initiator_socket.bind(interconnect.socket_A_in);
  chiplet2.bus.interconnect_initiator_socket.bind(interconnect.socket_B_in);
  interconnect.socket_A_out.bind(chiplet1.bus.interconnect_target_socket);
  interconnect.socket_B_out.bind(chiplet2.bus.interconnect_target_socket);

  sc_start(200, SC_NS);

  return 0;
}