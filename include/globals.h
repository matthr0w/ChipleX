#pragma once

#include <random>
#include <string>
#include <systemc>

using namespace sc_core;

// ---------------------------
// Logging
// ---------------------------
enum class LogLevel {
	DEBUG,
	DELAY,
	INFO,
	WARN,
	ERROR,
	SILENT
};
inline LogLevel log_level = LogLevel::INFO;

// ---------------------------
// Simulation Parameters
// ---------------------------
// No default setup: --setup selects one and the loader rejects an unknown name.
inline std::string sim_setup      = "";
inline std::string stats_out      = "stats.json";
inline sc_time     sim_duration   = sc_time(0, SC_NS);
inline double      wire_length_mm = 1.0;
inline double      wire_ps_per_mm = 5.0;
inline double      bit_error_rate = 1e-12;
// Deterministic by default; override with --seed.
inline constexpr unsigned               kDefaultRngSeed = 0xC0FFEE;
inline unsigned                         rng_seed        = kDefaultRngSeed;
inline std::mt19937                     bit_error_gen{kDefaultRngSeed};
inline std::uniform_real_distribution<> bit_error_dist{0.0, 1.0};