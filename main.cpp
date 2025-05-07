#include <systemc>
#include <vector>

#include "chiplet/Chiplet.h"
#include "chiplet/Config.h"
#include "fpga/Config.h"
#include "fpga/FPGA.h"

#include "common/RoutingTable.h"

#include "include/globals.h"
#include "include/logging.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

bool print_debug_msgs = false;
unsigned int num_chiplets = 2;
std::vector<unsigned int> connections = {1, 2};

void print_help(const char *progname) {
  std::cout
      << "Usage: " << progname << " [options]\n"
      << "Options:\n"
      << "  --time=<ns>          Set simulation time in nanoseconds (default: "
         "1000)\n"
      << "  --chiplets=<n>       Set number of chiplets (minimum: 2, default: "
         "2)\n"
      << "  --connections=1,2,3  Set FPGA connection targets (1,2,...,n)\n"
      << "  --debug              Enable debug messages\n"
      << "  --help               Show this help message\n";
}

bool parse_connections(const std::string &arg,
                       std::vector<unsigned int> &result) {
  std::stringstream stream(arg);
  std::string token;
  while (std::getline(stream, token, ',')) {
    try {
      unsigned int value = std::stoul(token);
      result.push_back(value);
    } catch (...) {
      return false;
    }
  }

  if (result.empty() || result.front() == 0 || result.back() > num_chiplets) {
    return false;
  }

  return true;
}

int sc_main(int argc, char *argv[]) {
  std::cout << "\n";
  sc_time sim_duration(1000, SC_NS);

  // parse command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help") {
      print_help(argv[0]);
      return 0;
    } else if (arg.rfind("--time=", 0) == 0) {
      try {
        double value = std::stod(arg.substr(7));
        sim_duration = sc_core::sc_time(value, sc_core::SC_NS);
      } catch (...) {
        std::cerr << "Invalid value for --time\n";
        return 1;
      }
    } else if (arg.rfind("--chiplets=", 0) == 0) {
      try {
        num_chiplets = std::stoul(arg.substr(11));
        if (num_chiplets < 2) {
          std::cerr << "Number of chiplets must be at least 2\n";
          return 1;
        }
      } catch (...) {
        std::cerr << "Invalid value for --chiplets\n";
        return 1;
      }
    } else if (arg.rfind("--connections=", 0) == 0) {
      connections.clear();
      std::string list = arg.substr(14);
      if (!parse_connections(list, connections)) {
        std::cerr << "Invalid connection list format\n";
        return 1;
      }
    } else if (arg == "--debug") {
      print_debug_msgs = true;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_help(argv[0]);
      return 1;
    }
  }

  // load chiplet config
  try {
    chiplet::Config::instance().loadFromFile("config_chiplet.yaml");
    chiplet::Config::instance().printConfig();
  } catch (...) {
    std::cerr << "Failed to load Chiplet configuration. Exiting.\n";
    return 1;
  }

  // load FPGA config
  try {
    fpga::Config::instance().loadFromFile("config_fpga.yaml");
    fpga::Config::instance().printConfig();
  } catch (...) {
    std::cerr << "Failed to load FPGA configuration. Exiting.\n";
    return 1;
  }

  // print simulation setup
  std::cout << "\n=== Simulation Setup ===\n";
  std::cout << "Simulation time: " << sim_duration << "\n";
  std::cout << "Number of chiplets: " << num_chiplets << "\n";
  std::cout << "FPGA connection list: ";
  for (auto c : connections)
    std::cout << c << " ";
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