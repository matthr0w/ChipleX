#include "chiplet/Chiplet.h"

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

  chiplet0.interconnect.interconnect_initiator_socket.bind(
      chiplet1.interconnect.interconnect_target_socket);
  chiplet1.interconnect.interconnect_initiator_socket.bind(
      chiplet0.interconnect.interconnect_target_socket);

  sc_start(sim_duration);

  return 0;
}