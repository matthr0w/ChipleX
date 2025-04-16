#include "chiplet/Chiplet.h"
#include "interconnect/Interconnect.h"

#include <systemc>

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

bool debug_msgs = false;

int sc_main(int argc, char *argv[]) {
  sc_time sim_duration(1000, SC_NS);

  // parse command line arguments
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--time=", 7) == 0) {
      double value = std::atof(argv[i] + 7);
      sim_duration = sc_time(value, SC_NS);
    } else if (std::strcmp(argv[i], "--debug") == 0) {
      debug_msgs = true;
    }
  }

  Chiplet chiplet0("Chiplet0");
  Chiplet chiplet1("Chiplet1");
  Interconnect interconnect("Interconnect");

  chiplet0.bus.interconnect_initiator_socket.bind(interconnect.socket_A_in);
  chiplet1.bus.interconnect_initiator_socket.bind(interconnect.socket_B_in);
  interconnect.socket_A_out.bind(chiplet0.bus.interconnect_target_socket);
  interconnect.socket_B_out.bind(chiplet1.bus.interconnect_target_socket);

  sc_start(sim_duration);

  return 0;
}