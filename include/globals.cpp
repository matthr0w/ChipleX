#include "globals.h"

LogLevel log_level = LogLevel::ERROR;

sc_time sim_duration = sc_time(1000, SC_NS);

unsigned int num_chiplets = 2;

ConnectionType connection_type = ConnectionType::Custom;
const char *to_string(ConnectionType type) {
  switch (type) {
  case ConnectionType::Custom:
    return "Custom";
  case ConnectionType::UCIe:
    return "UCIe";
  case ConnectionType::PCIe:
    return "PCIe";
  case ConnectionType::SPI:
    return "SPI";
  default:
    return "Unknown";
  }
}

std::vector<unsigned int> connections = {1, 2};

double chiplet_distance_um = 100;
double fpga_distance_mm = 5000;
double wire_ps_per_mm = 5.0;

double bit_error_rate = 1e-12;
std::mt19937 bit_error_gen(std::random_device{}());
std::uniform_real_distribution<> bit_error_dist(0.0, 1.0);