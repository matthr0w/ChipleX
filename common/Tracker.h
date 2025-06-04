#pragma once

#include <mutex>
#include <systemc>

#include "include/configs.h"

using namespace sc_core;

class LatencyTracker {
public:
  static LatencyTracker &instance() {
    static LatencyTracker instance;
    return instance;
  }

  void record(sc_time latency) {
    std::lock_guard<std::mutex> lock(mutex);
    ++transactions_count;
    total_latency += latency;
    if (latency < min_latency) {
      min_latency = latency;
    }
    if (latency > max_latency) {
      max_latency = latency;
    }
  }

  void report() {
    std::cout << "  Finished Transactions: " << transactions_count << std::endl;
    if (transactions_count > 0) {
      std::cout << "  Average:  " << total_latency / transactions_count
                << std::endl;
      std::cout << "  Minimum:  " << min_latency << std::endl;
      std::cout << "  Maximum:  " << max_latency << std::endl;
    }
  }

private:
  mutable std::mutex mutex;

  unsigned int transactions_count = 0;

  sc_time total_latency = SC_ZERO_TIME;
  sc_time min_latency = sc_max_time();
  sc_time max_latency = SC_ZERO_TIME;
};

class TransmissionTracker {
public:
  static TransmissionTracker &instance() {
    static TransmissionTracker instance;
    return instance;
  }

  void record_transmission(sc_time duration) {
    std::lock_guard<std::mutex> lock(mutex);
    ++transmission_count;
    total_duration += duration;
  }

  void record_attempt() {
    std::lock_guard<std::mutex> lock(mutex);
    ++attempt_count;
  }

  void report() {
    std::cout << "  Total Transmissions: " << transmission_count << std::endl;
    std::cout << "  Retries/FECs:        " << attempt_count << std::endl;
    std::cout << "  Flit Overhead:       " << calculate_overhead() << "%\n";
    std::cout << "  Power Dissipation:   " << calculate_power_mW() << " mW\n";
  }

private:
  mutable std::mutex mutex;

  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  const unsigned int flit_size =
      interconnect_config.get<unsigned int>("interconnect_protocol.flit_size");
  const unsigned int header_size = interconnect_config.get<unsigned int>(
      "interconnect_protocol.header_size");
  const double efficiency =
      interconnect_config.get<double>("interconnect.efficiency");

  unsigned int transmission_count = 0;
  unsigned int attempt_count = 0;

  sc_time total_duration = SC_ZERO_TIME;

  double calculate_overhead() {
    double overhead =
        static_cast<double>(header_size) / static_cast<double>(flit_size) * 100;
    return overhead;
  }

  double calculate_power_mW() {
    double total_energy_pJ = transmission_count * flit_size * 8 * efficiency;
    double total_energy_J = total_energy_pJ * 1e-12;
    if (total_duration > SC_ZERO_TIME) {
      return total_energy_J / total_duration.to_seconds() * 1000;
    } else {
      return 0;
    }
  }
};

class UtilizationTracker {
public:
  UtilizationTracker(const std::string &name) : name(name) {}

  void set_active() {
    if (active_count == 0) {
      last_active_time = sc_time_stamp();
    }
    active_count++;
  }

  void set_idle() {
    if (active_count == 0) {
      return;
    }
    active_count--;
    if (active_count == 0) {
      accumulated_active_time += sc_time_stamp() - last_active_time;
    }
  }

  void report() {
    if (active_count > 0) {
      accumulated_active_time += sc_time_stamp() - last_active_time;
    }

    double utilization =
        accumulated_active_time.to_seconds() / sc_time_stamp().to_seconds();
    std::cout << "  " << name << ": " << (utilization * 100.0) << "%\n";
  }

private:
  std::string name;

  unsigned int active_count = 0;

  sc_time accumulated_active_time = SC_ZERO_TIME;
  sc_time last_active_time = SC_ZERO_TIME;
};

class BufferUsageTracker {
public:
  BufferUsageTracker(const std::string &name) : name(name) {}

  void update(unsigned int usage) {
    sc_time delta_time = sc_time_stamp() - last_update_time;
    cumulative_usage += last_usage * delta_time.to_seconds(); // usage * time
    last_update_time = sc_time_stamp();
    last_usage = usage;
    if (usage > max_usage) {
      max_usage = usage;
    }
  }

  double get_average() {
    return cumulative_usage / sc_time_stamp().to_seconds();
  }

  void report() {
    std::cout << "  " << name << " (Average | Maximum): " << get_average()
              << " | " << max_usage << " bytes\n";
  }

private:
  std::string name;

  double cumulative_usage = 0.0; // in bytes * seconds
  unsigned int max_usage = 0;
  unsigned int last_usage = 0;

  sc_time last_update_time = SC_ZERO_TIME;
};