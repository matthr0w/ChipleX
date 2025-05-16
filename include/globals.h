#pragma once

#include <systemc>
#include <vector>

using namespace sc_core;

enum class LogLevel { DEBUG, INFO, WARN, ERROR, SILENT };

enum class ConnectionType { Custom, UCIe, PCIe, SPI, Unknown };

extern LogLevel log_level;

extern sc_time sim_duration;

extern unsigned int num_chiplets;

extern ConnectionType connection_type;
extern std::vector<unsigned int> connections;

const char *to_string(ConnectionType type);