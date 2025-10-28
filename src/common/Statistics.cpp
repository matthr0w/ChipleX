#include "common/Statistics.h"

#include <fstream>

#include "logging.h"

void StatisticsManager::register_module(const std::string &name) {
  if (modules_.find(name) == modules_.end())
    modules_[name] = ModuleUtilization();
}

void StatisticsManager::register_value(const std::string &name) {
  auto it = stats_.find(name);
  if (it == stats_.end() || !it->second)
    stats_[name] = std::make_unique<StatValue>();
}

void StatisticsManager::register_counter(const std::string &name) {
  auto it = stats_.find(name);
  if (it == stats_.end() || !it->second)
    stats_[name] = std::make_unique<StatCounter>();
}

void StatisticsManager::set_active(const std::string &name) {
  auto &module = modules_[name];
  sc_time now = sc_time_stamp();

  if (module.active_count == 0) {
    module.idle_time += now - module.last_change;
    module.active = true;
    module.last_change = now;
  }

  module.active_count++;
}

void StatisticsManager::set_idle(const std::string &name) {
  auto &module = modules_[name];
  sc_time now = sc_time_stamp();

  if (module.active_count == 0) {
    LOG_WARN("StatisticsManager: set_idle() called without matching "
             "set_active() for module " +
             name);
    return;
  }

  module.active_count--;

  if (module.active_count == 0) {
    module.active_time += now - module.last_change;
    module.active = false;
    module.last_change = now;
  }
}

void StatisticsManager::set_value(const std::string &name, double value) {
  register_value(name);
  auto *stat_value = dynamic_cast<StatValue *>(stats_[name].get());
  stat_value->set(value);
}

void StatisticsManager::increment_counter(const std::string &name,
                                          uint64_t value) {
  register_counter(name);
  auto *stat_counter = dynamic_cast<StatCounter *>(stats_[name].get());
  stat_counter->increment(value);
}

void StatisticsManager::start_simulation_timer() {
  sim_start_wall_ = high_resolution_clock::now();
  sim_start_sim_ = sc_time_stamp();
}

void StatisticsManager::end_simulation_timer() {
  double sim_exec_s =
      duration<double>(high_resolution_clock::now() - sim_start_wall_).count();
  double sim_time_ns = (sc_time_stamp() - sim_start_sim_).to_seconds() * 1e9;

  set_value("execution_time_s", sim_exec_s);
  set_value("simulation_time_ns", sim_time_ns);
}

void StatisticsManager::dump_to_file(const std::string &filename) {
  std::ofstream ofs(filename);
  ofs << "{\n";

  // Dump general stats
  ofs << "  \"stats\": {\n";
  for (auto it = stats_.begin(); it != stats_.end(); ++it) {
    ofs << "    \"" << it->first << "\": ";
    it->second->dump(ofs);
    if (std::next(it) != stats_.end())
      ofs << ",";
    ofs << "\n";
  }
  ofs << "  },\n";

  // Dump utilization
  ofs << "  \"modules\": {\n";
  for (auto it = modules_.begin(); it != modules_.end(); ++it) {
    auto &module = it->second;
    sc_time now = sc_time_stamp();
    sc_time total =
        module.active_time + module.idle_time + (now - module.last_change);
    double util = (module.active_time / total) * 100.0;
    ofs << "    \"" << it->first << "\": { \"utilization_percent\": " << util
        << " }";
    if (std::next(it) != modules_.end())
      ofs << ",";
    ofs << "\n";
  }
  ofs << "  }\n";

  ofs << "}\n";
}