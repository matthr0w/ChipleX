#include <systemc>
#include <vector>

#include "chiplet/Chiplet.h"

#include "common/RoutingTable.h"

#include "include/globals.h"
#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

bool print_debug_msgs = false;
unsigned int num_chiplets = 2;

int sc_main(int argc, char *argv[]) {
  sc_time sim_duration(1000, SC_NS);

  // parse command line arguments
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--time=", 7) == 0) {
      double value = std::atof(argv[i] + 7);
      sim_duration = sc_time(value, SC_NS);
    } else if (std::strncmp(argv[i], "--chiplets=", 11) == 0) {
      num_chiplets = std::atoi(argv[i] + 11);
      if (num_chiplets < 2) {
        SC_REPORT_ERROR("System", "Number of chiplets must be at least 2");
        return 1;
      }
    } else if (std::strcmp(argv[i], "--debug") == 0) {
      print_debug_msgs = true;
    }
  }

  RoutingTable::initialize(num_chiplets);

  // create chiplets
  std::vector<Chiplet *> chiplets;
  chiplets.reserve(num_chiplets);

  for (unsigned int i = 0; i < num_chiplets; ++i) {
    std::string name = "Chiplet" + std::to_string(i);
    chiplets.push_back(new Chiplet(name.c_str()));
  }

  // connect chiplets in a ring topology
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    int next = (i + 1) % num_chiplets;
    int prev = (i - 1 + num_chiplets) % num_chiplets;

    // connect interconnect1 to next chiplet interconnect0
    chiplets[i]->interconnect1.interconnect_initiator_socket.bind(
        chiplets[next]->interconnect0.interconnect_target_socket);

    // connect interconnect0 to previous chiplet interconnect1
    chiplets[i]->interconnect0.interconnect_initiator_socket.bind(
        chiplets[prev]->interconnect1.interconnect_target_socket);
  }

  sc_start(sim_duration);

  return 0;
}