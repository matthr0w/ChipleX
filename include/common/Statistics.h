#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <systemc>

using namespace sc_core;
using namespace std::chrono;

// -------------------------------------------------------
// StatTypes
// -------------------------------------------------------
class StatBase {
public:
  virtual ~StatBase() {}
  virtual void dump(std::ostream &os) const = 0;
};

class StatValue : public StatBase {
private:
  double value = 0.0;

public:
  void set(double v) { value = v; }
  void dump(std::ostream &os) const override { os << value; }
};

class StatCounter : public StatBase {
private:
  uint64_t value = 0;

public:
  void increment(uint64_t v = 1) { value += v; }
  void dump(std::ostream &os) const override { os << value; }
};

class StatAccum : public StatBase {
private:
  double value = 0;

public:
  void update(double v) { value += v; }
  void dump(std::ostream &os) const override { os << value; }
};

class StatMinMax : public StatBase {
private:
  bool initialized = false;
  double min = 0.0;
  double max = 0.0;

public:
  void update(double value);
  void dump(std::ostream &os) const override;
};

class StatUsage : public StatBase {
private:
  double cumulative_usage = 0.0;
  unsigned max_usage = 0;
  unsigned last_usage = 0;
  sc_time last_update = sc_time_stamp();

public:
  void update(unsigned usage);
  void dump(std::ostream &os) const override;
};

class StatUtilization : public StatBase {
private:
  sc_time clk_cycle;

  // Event-based tracking
  bool active = false;
  unsigned active_count = 0;
  sc_time last_change = sc_time_stamp();
  sc_time active_time_events = SC_ZERO_TIME;
  sc_time idle_time_events = SC_ZERO_TIME;

  // Manual tracking
  double active_cycle_fraction = 0.0;
  sc_time active_time_manual = SC_ZERO_TIME;
  sc_time idle_time_manual = SC_ZERO_TIME;

  bool event_activity = false;

public:
  explicit StatUtilization(sc_time clk_cycle) : clk_cycle(clk_cycle) {}

  void set_active();
  void set_idle();
  void add_active_time(const sc_time delta);
  void add_idle_time(const sc_time delta);
  void mark_active_cycle(double fraction = 1.0);
  void end_cycle();
  void dump(std::ostream &os) const override;
};

// -------------------------------------------------------
// StatManager
// -------------------------------------------------------
class StatManager {
public:
  static StatManager &instance() {
    static StatManager inst;
    return inst;
  }

  void register_value(const std::string &module, const std::string &name);
  void register_counter(const std::string &module, const std::string &name);
  void register_accum(const std::string &module, const std::string &name);
  void register_minmax(const std::string &module, const std::string &name);
  void register_usage(const std::string &module, const std::string &name);
  void register_utilization(const std::string &module,
                            const sc_time clk_cycle = sc_time(0, SC_NS));
  void register_utilization(const std::string &module, const std::string &name,
                            const sc_time clk_cycle = sc_time(0, SC_NS));

  void set_value(const std::string &module, const std::string &name,
                 double value);
  void increment_counter(const std::string &module, const std::string &name,
                         uint64_t value = 1);
  void update_accum(const std::string &module, const std::string &name,
                    double value);
  void update_minmax(const std::string &module, const std::string &name,
                     double value);
  void update_usage(const std::string &module, const std::string &name,
                    unsigned value);

  void set_active(const std::string &module);
  void set_active(const std::string &module, const std::string &name);
  void set_idle(const std::string &module);
  void set_idle(const std::string &module, const std::string &name);
  void add_active_time(const std::string &module,
                       const sc_time delta = sc_time(0, SC_NS));
  void add_active_time(const std::string &module, const std::string &name,
                       const sc_time delta = sc_time(0, SC_NS));
  void add_idle_time(const std::string &module,
                     const sc_time delta = sc_time(0, SC_NS));
  void add_idle_time(const std::string &module, const std::string &name,
                     const sc_time delta = sc_time(0, SC_NS));
  void mark_active_cycle(const std::string &module, double fraction = 1.0);
  void mark_active_cycle(const std::string &module, const std::string &name,
                         double fraction = 1.0);
  void end_cycle(const std::string &module);
  void end_cycle(const std::string &module, const std::string &name);

  void start_simulation_timer();
  void end_simulation_timer();

  void dump_to_file(const std::string &filename);

private:
  std::map<std::string, std::map<std::string, std::unique_ptr<StatBase>>>
      module_stats_;

  time_point<high_resolution_clock> time_wall_start_;
  double time_wall_value_ = 0.0;
  sc_time time_sim_start_;
  double time_sim_value_ = 0.0;

  StatManager() = default;
};