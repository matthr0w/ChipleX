#include "common/Statistics.h"

#include <fstream>

#include "logging.h"

// -------------------------------------------------------
// StatTypes Implementations
// -------------------------------------------------------
void StatMinMax::update(double value) {
  if (!initialized) {
    min = max = value;
    initialized = true;
  } else {
    if (value < min)
      min = value;
    if (value > max)
      max = value;
  }
}

void StatMinMax::dump(std::ostream &os) const {
  os << "{ "
     << "\"min\": " << (initialized ? min : 0) << ", "
     << "\"max\": " << (initialized ? max : 0) << " }";
}

void StatUsage::update(unsigned usage) {
  sc_time now = sc_time_stamp();
  sc_time delta = now - last_update;
  // Integrate usage over time (time-weighted average)
  cumulative_usage += last_usage * delta.to_seconds();
  last_usage = usage;
  last_update = now;
  if (usage > max_usage)
    max_usage = usage;
}

void StatUsage::dump(std::ostream &os) const {
  double sim_time = sc_time_stamp().to_seconds();
  double average_usage = 0.0;
  if (sim_time > 0.0)
    average_usage = cumulative_usage / sim_time;
  os << "{ \"average_usage\": " << average_usage
     << ", \"max_usage\": " << max_usage << " }";
}

void StatUtilization::set_active() {
  sc_time now = sc_time_stamp();
  event_activity = true;
  if (!active) {
    idle_time_events += (now - last_change);
    active = true;
  }
  active_count++;
  last_change = now;
}

void StatUtilization::set_idle() {
  if (active_count == 0)
    return;
  event_activity = true;
  active_count--;
  if (active_count == 0) {
    sc_time now = sc_time_stamp();
    active_time_events += (now - last_change);
    active = false;
    last_change = now;
  }
}

void StatUtilization::add_active_time(const sc_time delta) {
  active_time_manual += delta;
}

void StatUtilization::add_idle_time(const sc_time delta) {
  idle_time_manual += delta;
}

void StatUtilization::mark_active_cycle(const double fraction) {
  active_cycle_fraction = std::min(1.0, active_cycle_fraction + fraction);
}

void StatUtilization::end_cycle() {
  active_time_manual += clk_cycle * active_cycle_fraction;
  active_cycle_fraction = 0.0;
}

void StatUtilization::dump(std::ostream &os) const {
  sc_time now = sc_time_stamp();

  sc_time active_total;
  sc_time idle_total;

  if (event_activity) {
    sc_time active_ev = active_time_events;
    sc_time idle_ev = idle_time_events;
    if (active)
      active_ev += (now - last_change);
    else
      idle_ev += (now - last_change);

    active_total = std::min(active_ev + active_time_manual, now);
    idle_total = idle_ev + idle_time_manual;
  } else {
    active_total = std::min(active_time_manual, now);
    sc_time idle_est = now - active_total - idle_time_manual;
    if (idle_est < SC_ZERO_TIME)
      idle_est = SC_ZERO_TIME;
    idle_total = idle_time_manual + idle_est;
  }

  sc_time total = active_total + idle_total;
  double utilization = (total > SC_ZERO_TIME)
                           ? active_total.to_seconds() / total.to_seconds()
                           : 0.0;

  os << "{ "
     << "\"percentage\": " << utilization * 100.0 << ", "
     << "\"active_time_us\": " << active_total.to_seconds() * 1e6 << ", "
     << "\"idle_time_us\": " << idle_total.to_seconds() * 1e6 << " }";
}

// -------------------------------------------------------
// StatManager Implementation
// -------------------------------------------------------
void StatManager::register_value(const std::string &module,
                                 const std::string &name) {
  auto &stats = module_stats_[module];
  auto it = stats.find(name);
  if (it == stats.end() || !it->second)
    stats[name] = std::make_unique<StatValue>();
}

void StatManager::register_counter(const std::string &module,
                                   const std::string &name) {
  auto &stats = module_stats_[module];
  auto it = stats.find(name);
  if (it == stats.end() || !it->second)
    stats[name] = std::make_unique<StatCounter>();
}

void StatManager::register_accum(const std::string &module,
                                 const std::string &name) {
  auto &stats = module_stats_[module];
  auto it = stats.find(name);
  if (it == stats.end() || !it->second)
    stats[name] = std::make_unique<StatAccum>();
}

void StatManager::register_minmax(const std::string &module,
                                  const std::string &name) {
  auto &stats = module_stats_[module];
  auto it = stats.find(name);
  if (it == stats.end() || !it->second)
    stats[name] = std::make_unique<StatMinMax>();
}

void StatManager::register_usage(const std::string &module,
                                 const std::string &name) {
  auto &stats = module_stats_[module];
  auto it = stats.find(name);
  if (it == stats.end() || !it->second)
    stats[name] = std::make_unique<StatUsage>();
}

void StatManager::register_utilization(const std::string &module,
                                       const sc_time clk_cycle) {
  auto &stats = module_stats_[module];
  auto it = stats.find("utilization");
  if (it == stats.end() || !it->second)
    stats["utilization"] = std::make_unique<StatUtilization>(clk_cycle);
}

void StatManager::register_utilization(const std::string &module,
                                       const std::string &name,
                                       const sc_time clk_cycle) {
  auto &stats = module_stats_[module];
  auto it = stats.find(name);
  if (it == stats.end() || !it->second)
    stats[name] = std::make_unique<StatUtilization>(clk_cycle);
}

void StatManager::set_value(const std::string &module, const std::string &name,
                            double value) {
  register_value(module, name);
  auto *stat = dynamic_cast<StatValue *>(module_stats_[module][name].get());
  stat->set(value);
}

void StatManager::increment_counter(const std::string &module,
                                    const std::string &name, uint64_t value) {
  register_counter(module, name);
  auto *stat = dynamic_cast<StatCounter *>(module_stats_[module][name].get());
  stat->increment(value);
}

void StatManager::update_accum(const std::string &module,
                               const std::string &name, double value) {
  register_accum(module, name);
  auto *stat = dynamic_cast<StatAccum *>(module_stats_[module][name].get());
  stat->update(value);
}

void StatManager::update_minmax(const std::string &module,
                                const std::string &name, double value) {
  register_minmax(module, name);
  auto *stat = dynamic_cast<StatMinMax *>(module_stats_[module][name].get());
  stat->update(value);
}

void StatManager::update_usage(const std::string &module,
                               const std::string &name, unsigned value) {
  register_usage(module, name);
  auto *stat = dynamic_cast<StatUsage *>(module_stats_[module][name].get());
  stat->update(value);
}

void StatManager::set_active(const std::string &module) {
  auto *stat = dynamic_cast<StatUtilization *>(
      module_stats_[module]["utilization"].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " utilization not registered");
  stat->set_active();
}

void StatManager::set_active(const std::string &module,
                             const std::string &name) {
  auto *stat =
      dynamic_cast<StatUtilization *>(module_stats_[module][name].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " " + name + " not registered");
  stat->set_active();
}

void StatManager::set_idle(const std::string &module) {
  auto *stat = dynamic_cast<StatUtilization *>(
      module_stats_[module]["utilization"].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " utilization not registered");
  stat->set_idle();
}

void StatManager::set_idle(const std::string &module, const std::string &name) {
  auto *stat =
      dynamic_cast<StatUtilization *>(module_stats_[module][name].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " " + name + " not registered");
  stat->set_idle();
}

void StatManager::add_active_time(const std::string &module,
                                  const sc_time delta) {
  auto *stat = dynamic_cast<StatUtilization *>(
      module_stats_[module]["utilization"].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " utilization not registered");
  stat->add_active_time(delta);
}

void StatManager::add_active_time(const std::string &module,
                                  const std::string &name,
                                  const sc_time delta) {
  auto *stat =
      dynamic_cast<StatUtilization *>(module_stats_[module][name].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " " + name + " not registered");
  stat->add_active_time(delta);
}

void StatManager::add_idle_time(const std::string &module,
                                const sc_time delta) {
  auto *stat = dynamic_cast<StatUtilization *>(
      module_stats_[module]["utilization"].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " utilization not registered");
  stat->add_idle_time(delta);
}

void StatManager::add_idle_time(const std::string &module,
                                const std::string &name, const sc_time delta) {
  auto *stat =
      dynamic_cast<StatUtilization *>(module_stats_[module][name].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " " + name + " not registered");
  stat->add_idle_time(delta);
}

void StatManager::mark_active_cycle(const std::string &module,
                                    const double fraction) {
  auto *stat = dynamic_cast<StatUtilization *>(
      module_stats_[module]["utilization"].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " utilization not registered");
  stat->mark_active_cycle(fraction);
}

void StatManager::mark_active_cycle(const std::string &module,
                                    const std::string &name,
                                    const double fraction) {
  auto *stat =
      dynamic_cast<StatUtilization *>(module_stats_[module][name].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " " + name + " not registered");
  stat->mark_active_cycle(fraction);
}

void StatManager::end_cycle(const std::string &module) {
  auto *stat = dynamic_cast<StatUtilization *>(
      module_stats_[module]["utilization"].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " utilization not registered");
  stat->end_cycle();
}

void StatManager::end_cycle(const std::string &module,
                            const std::string &name) {
  auto *stat =
      dynamic_cast<StatUtilization *>(module_stats_[module][name].get());
  if (!stat)
    LOG_ERROR("StatManager: " + module + " " + name + " not registered");
  stat->end_cycle();
}

void StatManager::start_simulation_timer() {
  time_wall_start_ = high_resolution_clock::now();
  time_sim_start_ = sc_time_stamp();
}

void StatManager::end_simulation_timer() {
  time_wall_value_ =
      duration<double>(high_resolution_clock::now() - time_wall_start_)
          .count() *
      1e3;
  time_sim_value_ = (sc_time_stamp() - time_sim_start_).to_seconds() * 1e6;
}

void StatManager::dump_to_file(const std::string &filename) {
  std::ofstream ofs(filename);
  ofs << "{\n";
  ofs << "  \"execution_time_ms\": " << time_wall_value_ << ",\n";
  ofs << "  \"simulation_time_us\": " << time_sim_value_ << ",\n";
  ofs << "  \"modules\": {\n";

  bool first_module = true;
  for (const auto &[module_name, stats] : module_stats_) {
    if (!first_module)
      ofs << ",\n";
    first_module = false;
    ofs << "    \"" << module_name << "\": {\n";

    bool first_stat = true;
    for (const auto &[stat_name, stat] : stats) {
      if (!first_stat)
        ofs << ",\n";
      first_stat = false;
      ofs << "      \"" << stat_name << "\": ";
      stat->dump(ofs);
    }

    ofs << "\n    }";
  }

  ofs << "\n  }\n}\n";
}