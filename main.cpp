#include <systemc>
#include <vector>

#include "common/RoutingTable.h"
#include "common/Tracker.h"

#include "include/configs.h"
#include "include/globals.h"
#include "include/parser.h"

#include "modules/Chiplet.h"
#include "modules/FPGA.h"

#include "usercode/UserCode.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

// TODO:
// + add write operation
// + fix cache handling
// + update interconnect
// + add bursts
// + add API methods
// + update delay handling
// + fix memory ownership
// + fix statistics

int sc_main(int argc, char *argv[]) {
  std::cout << "\n";
  Parser parser;
  int result = parser.parse(argc, argv);
  if (result != -1) {
    return result;
  }
  parser.print_args();
  std::cout << "\n";

  auto start_timestamp = std::chrono::high_resolution_clock::now();

  // load chiplet config
  try {
    ConfigRegistry::instance().add("Chiplet", "configs/Chiplet.yaml",
                                   ConfigRegistry::chiplet_specification);
  } catch (const std::exception &e) {
    std::cerr << "Failed to load chiplet configuration.\n";
    std::cerr << e.what() << std::endl;
    return 1;
  }

  // load FPGA config
  try {
    ConfigRegistry::instance().add("FPGA", "configs/FPGA.yaml",
                                   ConfigRegistry::fpga_specification);
  } catch (const std::exception &e) {
    std::cerr << "Failed to load FPGA configuration.\n";
    std::cerr << e.what() << std::endl;
    return 1;
  }

  // load connection config
  try {
    std::string filepath = std::string("configs/interconnects/") +
                           to_string(connection_type) + std::string(".yaml");
    std::set<std::string> interconnect_specification =
        ConfigRegistry::interconnect_specifications
            .find(to_string(connection_type))
            ->second;
    ConfigRegistry::instance().add("Interconnect", filepath,
                                   interconnect_specification);
  } catch (const std::exception &e) {
    std::cerr << "Failed to load " << to_string(connection_type)
              << " configuration.\n";
    std::cerr << e.what() << std::endl;
    return 1;
  }

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
  auto it_core = core_code.find({0, 0});
  if (it_core != core_code.end()) {
    fpga.core.thread_fn = it_core->second.first;
    fpga.core.interrupt_fn = it_core->second.second;
  }

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

  if (sim_duration == SC_ZERO_TIME) {
    sc_start();
  } else {
    sc_start(sim_duration);
  }

  auto stop_timestamp = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      stop_timestamp - start_timestamp);

  // -------------------------------------------------------
  // statistics
  // -------------------------------------------------------
  std::cout << "=== Statistics ===" << std::endl;
  std::cout << "--- General ---" << std::endl;
  // times
  std::cout << "Simulation Time: " << sc_time_stamp() << std::endl;
  std::cout << "Execution Time: " << std::dec << duration.count() << " ms\n";
  // latencies
  std::cout << "Transaction Latencies:" << std::endl;
  LatencyTracker::instance().report();
  // transmissions
  std::cout << "Interconnect Transmissions:" << std::endl;
  // flits to chiplets
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    std::string target = "Chiplet" + std::to_string(i + 1);

    unsigned int prev_index = (i == 0) ? num_chiplets - 1 : i - 1;
    std::string prev_source = "Chiplet" + std::to_string(prev_index + 1);
    unsigned int next_index = (i == num_chiplets - 1) ? 0 : i + 1;
    std::string next_source = "Chiplet" + std::to_string(next_index + 1);

    unsigned int fpga_to_chiplet =
        chiplets[i]->interconnects[0]->incoming_flits;
    unsigned int prev_to_chiplet =
        chiplets[i]->interconnects[1]->incoming_flits;
    unsigned int next_to_chiplet =
        chiplets[i]->interconnects[2]->incoming_flits;

    if (fpga_to_chiplet > 0) {
      std::cout << "  Flits from FPGA to " << target << ": " << fpga_to_chiplet
                << std::endl;
    }

    if (prev_to_chiplet > 0) {
      std::cout << "  Flits from " << prev_source << " to " << target << ": "
                << prev_to_chiplet << std::endl;
    }

    if (next_to_chiplet > 0) {
      std::cout << "  Flits from " << next_source << " to " << target << ": "
                << next_to_chiplet << std::endl;
    }
  }
  // flits to FPGA
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    std::string source = "Chiplet" + std::to_string(i + 1);

    unsigned int chiplet_to_fpga = fpga.interconnects[i]->incoming_flits;

    if (chiplet_to_fpga > 0) {
      std::cout << "  Flits from " << source << " to FPGA: " << chiplet_to_fpga
                << std::endl;
    }
  }
  TransmissionTracker::instance().report();
  std::cout << "\n";

  // FPGA
  std::string title = "--- FPGA ---";
  std::cout << title << std::endl;
  std::cout << "Utilizations:" << std::endl;
  for (unsigned int j = 0; j < fpga.utilization_trackers.size(); ++j) {
    fpga.utilization_trackers[j]->report();
  }
  std::cout << "Generator Cache:" << std::endl;
  fpga.cache.report_rates();
  std::cout << "RAM Usage:" << std::endl;
  fpga.ram.report_usage();
  std::cout << "Average Buffer Fill Levels:" << std::endl;
  for (unsigned int j = 0; j < fpga.buffer_trackers.size(); ++j) {
    fpga.buffer_trackers[j]->report();
  }
  std::cout << "\n";

  // chiplets
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    std::string title = "--- Chiplet" + std::to_string(i + 1) + " ---";
    std::cout << title << std::endl;
    std::cout << "Utilizations:" << std::endl;
    for (unsigned int j = 0; j < chiplets[i]->utilization_trackers.size();
         ++j) {
      chiplets[i]->utilization_trackers[j]->report();
    }
    std::cout << "Core0 Cache:" << std::endl;
    chiplets[i]->cache0.report_rates();
    std::cout << "Core1 Cache:" << std::endl;
    chiplets[i]->cache1.report_rates();
    std::cout << "RAM Usage:" << std::endl;
    chiplets[i]->ram.report_usage();
    std::cout << "Buffer Fill Levels:" << std::endl;
    for (unsigned int j = 0; j < chiplets[i]->buffer_trackers.size(); ++j) {
      chiplets[i]->buffer_trackers[j]->report();
    }
  }
  std::cout << "\n";

  // clean up
  for (auto *chiplet : chiplets) {
    delete chiplet;
  }
  chiplets.clear();

  return 0;
}