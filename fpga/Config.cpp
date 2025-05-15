#include "Config.h"

#include <iostream>

fpga::Config &fpga::Config::instance() {
  static Config instance;
  return instance;
}

void fpga::Config::loadFromFile(const std::string &filename) {
  YAML::Node config = YAML::LoadFile(filename);

  try {
    validateConfig(config);
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] " << e.what() << std::endl;
    throw;
  }

  bus_width = config["bus"]["width"].as<unsigned int>();
  bus_clk_cycle = sc_time(config["bus"]["clk_cycle"].as<double>(), SC_NS);
  bus_arbitration_delay =
      sc_time(config["bus"]["arbitration_delay"].as<double>(), SC_NS);

  ram_size = config["ram"]["size"].as<unsigned int>();
  ram_width = config["ram"]["width"].as<unsigned int>();
  ram_clk_cycle = sc_time(config["ram"]["clk_cycle"].as<double>(), SC_NS);
  ram_access_delay = sc_time(config["ram"]["access_delay"].as<double>(), SC_NS);

  interconnect_protocol_width =
      config["interconnect_protocol"]["width"].as<unsigned int>();
  interconnect_protocol_flit_size =
      config["interconnect_protocol"]["flit_size"].as<unsigned int>();
  interconnect_protocol_buffer_size =
      config["interconnect_protocol"]["buffer_size"].as<unsigned int>();
  interconnect_protocol_clk_cycle =
      sc_time(config["interconnect_protocol"]["clk_cycle"].as<double>(), SC_NS);

  interconnect_width = config["interconnect"]["width"].as<unsigned int>();
  interconnect_buffer_size =
      config["interconnect"]["buffer_size"].as<unsigned int>();
  interconnect_clk_cycle =
      sc_time(config["interconnect"]["clk_cycle"].as<double>(), SC_NS);
}

void fpga::Config::validateConfig(const YAML::Node &config) {
  auto check = [](const YAML::Node &node, const std::string &keyPath) {
    if (!node || node.IsNull()) {
      throw std::runtime_error("Missing config section: " + keyPath);
    }
  };

  check(config["bus"], "bus");
  check(config["bus"]["width"], "bus.width");
  check(config["bus"]["clk_cycle"], "bus.clk_cycle");
  check(config["bus"]["arbitration_delay"], "bus.arbitration_delay");

  check(config["ram"], "ram");
  check(config["ram"]["size"], "ram.size");
  check(config["ram"]["width"], "ram.width");
  check(config["ram"]["clk_cycle"], "ram.clk_cycle");
  check(config["ram"]["access_delay"], "ram.access_delay");

  check(config["interconnect_protocol"], "interconnect_protocol");
  check(config["interconnect_protocol"]["width"],
        "interconnect_protocol.width");
  check(config["interconnect_protocol"]["flit_size"],
        "interconnect_protocol.flit_size");
  check(config["interconnect_protocol"]["buffer_size"],
        "interconnect_protocol.buffer_size");
  check(config["interconnect_protocol"]["clk_cycle"],
        "interconnect_protocol.clk_cycle");

  check(config["interconnect"], "interconnect");
  check(config["interconnect"]["width"], "interconnect.width");
  check(config["interconnect"]["buffer_size"], "interconnect.buffer_size");
  check(config["interconnect"]["clk_cycle"], "interconnect.clk_cycle");
}

void fpga::Config::printConfig() const {
  std::cout << "\n=== FPGA Configuration ===\n";
  std::cout << "Bus:\n";
  std::cout << "  Width: " << bus_width << " bytes\n";
  std::cout << "  Clock Cycle: " << bus_clk_cycle << "\n";
  std::cout << "  Arbitration Delay: " << bus_arbitration_delay << "\n";

  std::cout << "RAM:\n";
  std::cout << "  Size: " << ram_size << " KB\n";
  std::cout << "  Width: " << ram_width << " bytes\n";
  std::cout << "  Clock Cycle: " << ram_clk_cycle << "\n";
  std::cout << "  Access Delay: " << ram_access_delay << "\n";

  std::cout << "Interconnect Protocol:\n";
  std::cout << "  Width: " << interconnect_protocol_width << " bytes\n";
  std::cout << "  Flit Size: " << interconnect_protocol_flit_size << " bytes\n";
  std::cout << "  Buffer Size: " << interconnect_protocol_buffer_size
            << " bytes\n";
  std::cout << "  Clock Cycle: " << interconnect_protocol_clk_cycle << "\n";

  std::cout << "Interconnect:\n";
  std::cout << "  Width: " << interconnect_width << " bytes\n";
  std::cout << "  Buffer Size: " << interconnect_buffer_size << " bytes\n";
  std::cout << "  Clock Cycle: " << interconnect_clk_cycle << "\n";
}