#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <systemc>

using namespace sc_core;
using namespace std::chrono;

class StatBase {
public:
  virtual ~StatBase() {}
  virtual void dump(std::ostream &os) const = 0;
};

class StatValue : public StatBase {
public:
  double value = 0.0;
  void set(double v) { value = v; }
  void dump(std::ostream &os) const override { os << value; }
};

class StatCounter : public StatBase {
public:
  uint64_t value = 0;
  void increment(uint64_t v = 1) { value += v; }
  void dump(std::ostream &os) const override { os << value; }
};

struct ModuleUtilization {
  sc_time last_change;
  bool active = false;
  unsigned active_count = 0;
  sc_time active_time = SC_ZERO_TIME;
  sc_time idle_time = SC_ZERO_TIME;

  ModuleUtilization() : last_change(sc_time_stamp()) {}
};

class StatisticsManager {
public:
  static StatisticsManager &instance() {
    static StatisticsManager inst;
    return inst;
  }

  void register_module(const std::string &name);
  void register_value(const std::string &name);
  void register_counter(const std::string &name);

  void set_active(const std::string &name);
  void set_idle(const std::string &name);

  void set_value(const std::string &name, double value);

  void increment_counter(const std::string &name, uint64_t value = 1);

  void start_simulation_timer();
  void end_simulation_timer();

  void dump_to_file(const std::string &filename);

private:
  std::map<std::string, std::unique_ptr<StatBase>> stats_;
  std::map<std::string, ModuleUtilization> modules_;

  time_point<high_resolution_clock> sim_start_wall_;
  sc_time sim_start_sim_;

  StatisticsManager() = default;
};