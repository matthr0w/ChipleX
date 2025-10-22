#pragma once

#include <random>
#include <string>
#include <systemc>

using namespace sc_core;

// ---------------------------
// Logging
// ---------------------------
enum class LogLevel { DEBUG, DELAY, INFO, WARN, ERROR, SILENT };
inline LogLevel log_level = LogLevel::WARN;

// ---------------------------
// Simulation Parameters
// ---------------------------
inline std::string sim_setup = "default";
inline sc_time sim_duration = sc_time(0, SC_NS);
inline double wire_length_mm = 1.0;
inline double wire_ps_per_mm = 5.0;
inline double bit_error_rate = 1e-12;
inline std::mt19937 bit_error_gen{std::random_device{}()};
inline std::uniform_real_distribution<> bit_error_dist{0.0, 1.0};