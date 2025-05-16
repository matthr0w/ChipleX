#include "parser.h"

#include <algorithm>
#include <iostream>
#include <sstream>

void Parser::print_help(const char *progname) {
  std::cout
      << "Usage: " << progname << " [options]\n"
      << "Options:\n"
      << "  --time=<ns>               Set simulation time in nanoseconds "
         "(default: 1000)\n"
      << "  --chiplets=<n>            Set number of chiplets (minimum: 2, "
         "default: 2)\n"
      << "  --connection-type=<type>  Set interconnect type: Custom, "
         "UCIe, PCIe, "
         "SPI (default: Custom)\n"
      << "  --connections=1,2,3       Set FPGA connection targets: "
         "1,2,...,n (default: 1,2)\n"
      << "  --chiplet-distance=<um>   Set distance between chiplets in "
         "micrometers (default: 100)\n"
      << "  --fpga-distance=<mm>      Set distance between FPGA and chiplets "
         "in millimeters (default: 5000)\n"
      << "  --ber=<prob>              Set bit error rate (default: 1e-12)\n"
      << "  --logging=level           Set logging level: INFO, WARN, "
         "ERROR, DEBUG, SILENT (default: SILENT)\n"
      << "  --help                    Show this help message\n";
}

void Parser::print_args() {
  std::cout << "=== Simulation Configuration ===\n"
            << "Simulation time: " << sim_duration << "\n"
            << "Number of chiplets: " << num_chiplets << "\n"
            << "Interconnect type: " << to_string(connection_type) << "\n"
            << "FPGA connection targets: ";
  for (auto c : connections) {
    std::cout << c << " ";
  }
  std::cout << "\n"
            << "Chiplet distance: " << chiplet_distance_um << " um\n"
            << "FPGA distance: " << fpga_distance_mm << " mm\n"
            << "Bit error rate: " << bit_error_rate << "\n";
}

bool Parser::parse_connection_list(const std::string &arg) {
  std::stringstream stream(arg);
  std::string token;
  connections.clear();

  while (std::getline(stream, token, ',')) {
    try {
      unsigned int value = std::stoul(token);
      connections.push_back(value);
    } catch (...) {
      return false;
    }
  }

  if (connections.empty() || connections.front() == 0 ||
      connections.back() > num_chiplets) {
    return false;
  }

  return true;
}

ConnectionType Parser::parse_connection_type(const std::string &value) {
  std::string type = value;
  std::transform(type.begin(), type.end(), type.begin(), ::toupper);

  if (type == "UCIE")
    return ConnectionType::UCIe;
  if (type == "PCIE")
    return ConnectionType::PCIe;
  if (type == "SPI")
    return ConnectionType::SPI;
  return ConnectionType::Unknown;
}

int Parser::parse(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help") {
      print_help(argv[0]);
      return 0;
    } else if (arg.rfind("--time=", 0) == 0) {
      try {
        double value = std::stod(arg.substr(7));
        sim_duration = sc_time(value, SC_NS);
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
      std::string list = arg.substr(14);
      if (!parse_connection_list(list)) {
        std::cerr << "Invalid connection list format\n";
        return 1;
      }
    } else if (arg.rfind("--connection-type=", 0) == 0) {
      std::string value = arg.substr(18);
      connection_type = parse_connection_type(value);
      if (connection_type == ConnectionType::Unknown) {
        std::cerr << "Unknown interconnect type: " << value << "\n";
        return 1;
      }
    } else if (arg.rfind("--chiplet-distance=", 0) == 0) {
      try {
        chiplet_distance_um = std::stod(arg.substr(19));
      } catch (...) {
        std::cerr << "Invalid value for --chiplet-distance\n";
        return 1;
      }
    } else if (arg.rfind("--fpga-distance=", 0) == 0) {
      try {
        fpga_distance_mm = std::stod(arg.substr(16));
      } catch (...) {
        std::cerr << "Invalid value for --fpga-distance\n";
        return 1;
      }
    } else if (arg.rfind("--ber=", 0) == 0) {
      try {
        bit_error_rate = std::stod(arg.substr(6));
      } catch (...) {
        std::cerr << "Invalid value for --ber\n";
        return 1;
      }
    } else if (arg.rfind("--logging=", 0) == 0) {
      std::string level = arg.substr(10);
      std::transform(level.begin(), level.end(), level.begin(), ::tolower);

      if (level == "info") {
        log_level = LogLevel::INFO;
      } else if (level == "warn") {
        log_level = LogLevel::WARN;
      } else if (level == "error") {
        log_level = LogLevel::ERROR;
      } else if (level == "debug") {
        log_level = LogLevel::DEBUG;
      } else if (level == "silent") {
        log_level = LogLevel::SILENT;
      } else {
        std::cerr << "Unknown logging level: " << level << "\n";
        print_help(argv[0]);
        return 1;
      }
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_help(argv[0]);
      return 1;
    }
  }

  return -1;
}