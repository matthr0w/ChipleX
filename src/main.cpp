#include <systemc>
#include <vector>

#include "globals.h"
#include "parser.h"

#include "common/RoutingTable.h"

#include "modules/Chiplet.h"

#include "usercode/UserCode.h"

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
  std::cout << "\n";

  auto start_timestamp = std::chrono::high_resolution_clock::now();

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

  // assign user code
  // chiplets
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    auto &chiplet = chiplets[i];

    for (unsigned int c = 0; c < chiplet->num_cores; ++c) {
      auto it = core_code.find({i + 1, c});
      if (it != core_code.end()) {
        chiplet->cores[c]->thread_fn = it->second.first;
        chiplet->cores[c]->interrupt_fn = it->second.second;
      }
    }
  }

  // connect chiplets in a ring topology
  for (unsigned i = 0; i < num_chiplets; ++i) {
    int next = (i + 1) % num_chiplets;
    int prev = (i - 1 + num_chiplets) % num_chiplets;

    // connect interconnect2 to next chiplet interconnect1
    chiplets[i]->interconnect->out_ports[2]->bind(
        *chiplets[next]->interconnect->in_ports[1]);

    // connect interconnect1 to previous chiplet interconnect2
    chiplets[i]->interconnect->out_ports[1]->bind(
        *chiplets[prev]->interconnect->in_ports[2]);
  }

  if (sim_duration == SC_ZERO_TIME) {
    sc_start();
  } else {
    sc_start(sim_duration);
  }

  auto stop_timestamp = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      stop_timestamp - start_timestamp);

  // -------------------------------------------------------
  // Statistics
  // -------------------------------------------------------
  std::cout << "=== Statistics ===" << std::endl;
  std::cout << "Simulation Time: " << sc_time_stamp() << std::endl;
  std::cout << "Execution Time: " << std::dec << duration.count() << " ms\n";

  for (auto *chiplet : chiplets) {
    delete chiplet;
  }
  chiplets.clear();

  return 0;
}