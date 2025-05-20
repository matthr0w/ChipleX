#include "Config.h"

#include <iostream>

chiplet::Config &chiplet::Config::instance() {
  static Config instance;
  return instance;
}

void chiplet::Config::load(const std::string &filename) {
  YAML::Node config = YAML::LoadFile(filename);

  try {
    validate(config);
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
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

  interconnect_protocol_buffer_size =
      config["interconnect_protocol"]["buffer_size"].as<unsigned int>();
  interconnect_protocol_flit_size =
      config["interconnect_protocol"]["flit_size"].as<unsigned int>();
  interconnect_protocol_header_size =
      config["interconnect_protocol"]["header_size"].as<unsigned int>();
  interconnect_protocol_pre_delay =
      sc_time(config["interconnect_protocol"]["pre_delay"].as<double>(), SC_NS);
  interconnect_protocol_post_delay = sc_time(
      config["interconnect_protocol"]["post_delay"].as<double>(), SC_NS);
  interconnect_protocol_irq_delay =
      sc_time(config["interconnect_protocol"]["irq_delay"].as<double>(), SC_NS);

  interconnect_buffer_size =
      config["interconnect"]["buffer_size"].as<unsigned int>();
  interconnect_bandwidth = config["interconnect"]["bandwidth"].as<double>();
}

void chiplet::Config::validate(const YAML::Node &config) {
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
  check(config["interconnect_protocol"]["buffer_size"],
        "interconnect_protocol.buffer_size");
  check(config["interconnect_protocol"]["flit_size"],
        "interconnect_protocol.flit_size");
  check(config["interconnect_protocol"]["header_size"],
        "interconnect_protocol.header_size");
  check(config["interconnect_protocol"]["pre_delay"],
        "interconnect_protocol.pre_delay");
  check(config["interconnect_protocol"]["post_delay"],
        "interconnect_protocol.post_delay");
  check(config["interconnect_protocol"]["irq_delay"],
        "interconnect_protocol.irq_delay");

  check(config["interconnect"], "interconnect");
  check(config["interconnect"]["buffer_size"], "interconnect.buffer_size");
  check(config["interconnect"]["bandwidth"], "interconnect.bandwidth");
}

void chiplet::Config::override(const std::string &filename) {
  YAML::Node config = YAML::LoadFile(filename);

  if (config["bus"]) {
    if (config["bus"]["width"])
      bus_width = config["bus"]["width"].as<unsigned int>();
    if (config["bus"]["clk_cycle"])
      bus_clk_cycle = sc_time(config["bus"]["clk_cycle"].as<double>(), SC_NS);
    if (config["bus"]["arbitration_delay"])
      bus_arbitration_delay =
          sc_time(config["bus"]["arbitration_delay"].as<double>(), SC_NS);
  }

  if (config["ram"]) {
    if (config["ram"]["size"])
      ram_size = config["ram"]["size"].as<unsigned int>();
    if (config["ram"]["width"])
      ram_width = config["ram"]["width"].as<unsigned int>();
    if (config["ram"]["clk_cycle"])
      ram_clk_cycle = sc_time(config["ram"]["clk_cycle"].as<double>(), SC_NS);
    if (config["ram"]["access_delay"])
      ram_access_delay =
          sc_time(config["ram"]["access_delay"].as<double>(), SC_NS);
  }

  if (config["interconnect_protocol"]) {
    if (config["interconnect_protocol"]["buffer_size"])
      interconnect_protocol_buffer_size =
          config["interconnect_protocol"]["buffer_size"].as<unsigned int>();
    if (config["interconnect_protocol"]["flit_size"])
      interconnect_protocol_flit_size =
          config["interconnect_protocol"]["flit_size"].as<unsigned int>();
    if (config["interconnect_protocol"]["header_size"])
      interconnect_protocol_header_size =
          config["interconnect_protocol"]["header_size"].as<unsigned int>();
    if (config["interconnect_protocol"]["pre_delay"])
      interconnect_protocol_pre_delay = sc_time(
          config["interconnect_protocol"]["pre_delay"].as<double>(), SC_NS);
    if (config["interconnect_protocol"]["post_delay"])
      interconnect_protocol_post_delay = sc_time(
          config["interconnect_protocol"]["post_delay"].as<double>(), SC_NS);
    if (config["interconnect_protocol"]["irq_delay"])
      interconnect_protocol_irq_delay = sc_time(
          config["interconnect_protocol"]["irq_delay"].as<double>(), SC_NS);
  }

  if (config["interconnect"]) {
    if (config["interconnect"]["buffer_size"])
      interconnect_buffer_size =
          config["interconnect"]["buffer_size"].as<unsigned int>();
    if (config["interconnect"]["bandwidth"])
      interconnect_bandwidth = config["interconnect"]["bandwidth"].as<double>();
  }
}

void chiplet::Config::print() const {
  std::cout << "=== Chiplet Configuration ===\n"
            << "Bus:\n"
            << "  Width: " << bus_width << " bits\n"
            << "  Clock Cycle: " << bus_clk_cycle << "\n"
            << "  Arbitration Delay: " << bus_arbitration_delay << "\n"

            << "RAM:\n"
            << "  Size: " << ram_size << " KB\n"
            << "  Width: " << ram_width << " bits\n"
            << "  Clock Cycle: " << ram_clk_cycle << "\n"
            << "  Access Delay: " << ram_access_delay << "\n"

            << "Interconnect Protocol:\n"
            << "  Buffer Size: " << interconnect_protocol_buffer_size
            << " bytes\n"
            << "  Flit Size: " << interconnect_protocol_flit_size << " bytes\n"
            << "  Header Size: " << interconnect_protocol_header_size
            << " bytes\n"
            << "  Pre Delay: " << interconnect_protocol_pre_delay << "\n"
            << "  Post Delay: " << interconnect_protocol_post_delay << "\n"
            << "  IRQ Delay: " << interconnect_protocol_irq_delay << "\n"

            << "Interconnect:\n"
            << "  Buffer Size: " << interconnect_buffer_size << " bytes\n"
            << "  Bandwidth: " << interconnect_bandwidth << " Gb/s\n";
}