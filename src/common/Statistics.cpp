#include "common/Statistics.h"

#include <fstream>

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
  if (!active) {
    idle_time += (now - last_change);
    active = true;
  }
  active_count++;
  last_change = now;
}

void StatUtilization::set_idle() {
  if (active_count == 0)
    return;
  active_count--;
  if (active_count == 0) {
    sc_time now = sc_time_stamp();
    active_time += (now - last_change);
    active = false;
    last_change = now;
  }
}

void StatUtilization::dump(std::ostream &os) const {
  sc_time now = sc_time_stamp();
  sc_time active_time_ = active_time;
  sc_time idle_time_ = idle_time;
  if (active)
    active_time_ += now - last_change;
  else
    idle_time_ += now - last_change;

  sc_time total = active_time_ + idle_time_;
  double utilization = 0.0;
  if (total > SC_ZERO_TIME)
    utilization = active_time_.to_seconds() / total.to_seconds();

  os << "{ "
     << "\"percentage\": " << utilization * 100.0 << ", "
     << "\"active_time_us\": " << active_time_.to_seconds() * 1e6 << ", "
     << "\"idle_time_us\": " << idle_time_.to_seconds() * 1e6 << " }";
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
                                       const std::string &name) {
  auto &stats = module_stats_[module];
  auto it = stats.find(name);
  if (it == stats.end() || !it->second)
    stats[name] = std::make_unique<StatUtilization>();
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

void StatManager::set_active(const std::string &module,
                             const std::string &name) {
  register_utilization(module, name);
  auto *stat =
      dynamic_cast<StatUtilization *>(module_stats_[module][name].get());
  stat->set_active();
}

void StatManager::set_idle(const std::string &module, const std::string &name) {
  register_utilization(module, name);
  auto *stat =
      dynamic_cast<StatUtilization *>(module_stats_[module][name].get());
  stat->set_idle();
}

void StatManager::start_simulation_timer() {
  time_wall_start_ = high_resolution_clock::now();
  time_sim_start_ = sc_time_stamp();
}

void StatManager::end_simulation_timer() {
  time_wall_value_ =
      duration<double>(high_resolution_clock::now() - time_wall_start_)
          .count() *
      1e6;
  time_sim_value_ = (sc_time_stamp() - time_sim_start_).to_seconds() * 1e6;
}

void StatManager::dump_to_file(const std::string &filename) {
  std::ofstream ofs(filename);
  ofs << "{\n";
  ofs << "  \"execution_time_us\": " << time_wall_value_ << ",\n";
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