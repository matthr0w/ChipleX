#include <systemc>
#include <vector>

#include "chiplet/Chiplet.h"
#include "chiplet/Config.h"
#include "fpga/Config.h"
#include "fpga/FPGA.h"

#include "configs/UserCode.h"

#include "common/RoutingTable.h"

#include "include/globals.h"
#include "include/parser.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

int sc_main(int argc, char *argv[]) {
  std::cout << "\n";

  Parser parser;
  int result = parser.parse(argc, argv);
  if (result != -1) {
    return result;
  }

  // load chiplet config
  try {
    chiplet::Config::instance().load("configs/Chiplet.yaml");
  } catch (...) {
    std::cerr << "Failed to load Chiplet configuration. Exiting.\n";
    return 1;
  }

  // load FPGA config
  try {
    fpga::Config::instance().load("configs/FPGA.yaml");
  } catch (...) {
    std::cerr << "Failed to load FPGA configuration. Exiting.\n";
    return 1;
  }

  // load connection config
  if (connection_type != ConnectionType::Custom) {
    try {
      chiplet::Config::instance().override(
          std::string("configs/interconnects/") + to_string(connection_type) +
          std::string(".yaml"));
      // custom FPGA interconnect
      // fpga::Config::instance().override(std::string("configs/interconnects/")
      // + to_string(connection_type) + std::string(".yaml"));
    } catch (...) {
      std::cerr << "Failed to load interconnect configuration. Exiting.\n";
      return 1;
    }
  }

  // print configurations
  chiplet::Config::instance().print();
  std::cout << "\n";
  fpga::Config::instance().print();
  std::cout << "\n";
  parser.print_args();
  std::cout << "\n\n";

  // initialize routing table
  RoutingTable::initialize(num_chiplets);

  // create chiplets
  std::vector<Chiplet *> chiplets;
  chiplets.reserve(num_chiplets);

  for (unsigned int i = 0; i < num_chiplets; ++i) {
    std::string name =
        "Chiplet" + std::to_string(i + 1); // id == 0 is reserved for FPGA
    chiplets.push_back(new Chiplet(name.c_str()));
  }

  // create FPGA
  FPGA fpga("FPGA");

  // assign user code
  // chiplets
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    auto it_core0 = core_code.find({i + 1, 0});
    if (it_core0 != core_code.end()) {
      chiplets[i]->core0.thread_fn = it_core0->second.first;
      chiplets[i]->core0.interrupt_fn = it_core0->second.second;
    }
    auto it_core1 = core_code.find({i + 1, 1});
    if (it_core1 != core_code.end()) {
      chiplets[i]->core1.thread_fn = it_core1->second.first;
      chiplets[i]->core1.interrupt_fn = it_core1->second.second;
    }
  }
  // FPGA
  fpga.generator.gen_fn = generator_code.first;
  fpga.generator.interrupt_fn = generator_code.second;

  // connect chiplets in a ring topology
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    int next = (i + 1) % num_chiplets;
    int prev = (i - 1 + num_chiplets) % num_chiplets;

    // connect interconnect2 to next chiplet interconnect1
    chiplets[i]->interconnects[2]->interconnect_initiator_socket.bind(
        chiplets[next]->interconnects[1]->interconnect_target_socket);

    // connect interconnect1 to previous chiplet interconnect2
    chiplets[i]->interconnects[1]->interconnect_initiator_socket.bind(
        chiplets[prev]->interconnects[2]->interconnect_target_socket);
  }

  // connect chiplets to FPGA
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    // connect interconnect0 to FPGA interconnect
    chiplets[i]->interconnects[0]->interconnect_initiator_socket.bind(
        fpga.interconnects[i]->interconnect_target_socket);

    // connect FPGA interconnect to interconnect0
    fpga.interconnects[i]->interconnect_initiator_socket.bind(
        chiplets[i]->interconnects[0]->interconnect_target_socket);
  }

  sc_start(sim_duration);

  // clean up chiplets
  for (auto *chiplet : chiplets) {
    delete chiplet;
  }
  chiplets.clear();

  return 0;
}