#pragma once

#include <random>
#include <systemc>
#include <vector>

using namespace sc_core;

enum class LogLevel { DEBUG, DELAY, INFO, WARN, ERROR, SILENT };

enum class ConnectionType { Custom, PCIe, UCIe, SerialLink, SPI, Unknown };

extern LogLevel log_level;

extern sc_time sim_duration;

extern unsigned int num_chiplets;

extern ConnectionType connection_type;
const char *to_string(ConnectionType type);
extern std::vector<unsigned int> connections;

extern double chiplet_distance_um;
extern double fpga_distance_mm;
extern double wire_ps_per_mm;

extern double bit_error_rate;
extern std::mt19937 bit_error_gen;
extern std::uniform_real_distribution<> bit_error_dist;