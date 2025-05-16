#include "globals.h"

LogLevel log_level = LogLevel::SILENT;

sc_time sim_duration = sc_time(1000, SC_NS);

unsigned int num_chiplets = 2;

ConnectionType connection_type = ConnectionType::Custom;
std::vector<unsigned int> connections = {1, 2};

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