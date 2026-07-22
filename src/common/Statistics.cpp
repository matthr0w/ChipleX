#include "common/Statistics.h"

#include <algorithm>
#include <fstream>
#include <vector>

#include "logging.h"

// -------------------------------------------------------
// StatTypes Implementations
// -------------------------------------------------------
void StatMinMax::update(double value) {
	if (!initialized) {
		min = max   = value;
		initialized = true;
	} else {
		if (value < min) {
			min = value;
		}
		if (value > max) {
			max = value;
		}
	}
}

void StatMinMax::dump(std::ostream &os) const {
	os << "{ "
	   << "\"min\": " << (initialized ? min : 0) << ", "
	   << "\"max\": " << (initialized ? max : 0) << " }";
}

void StatUsage::update(unsigned usage) {
	sc_time now   = sc_time_stamp();
	sc_time delta = now - last_update;
	// Integrate usage over time (time-weighted average)
	cumulative_usage += last_usage * delta.to_seconds();
	last_usage        = usage;
	last_update       = now;
	if (usage > max_usage) {
		max_usage = usage;
	}
}

void StatUsage::dump(std::ostream &os) const {
	double sim_time      = sc_time_stamp().to_seconds();
	double average_usage = 0.0;
	if (sim_time > 0.0) {
		average_usage = cumulative_usage / sim_time;
	}
	os << "{ \"average_usage\": " << average_usage << ", \"max_usage\": " << max_usage << " }";
}

void StatUtilization::set_active() {
	sc_time now    = sc_time_stamp();
	event_activity = true;
	if (!active) {
		idle_time_events += (now - last_change);
		active            = true;
	}
	active_count++;
	last_change = now;
}

void StatUtilization::set_idle() {
	if (active_count == 0) {
		return;
	}
	event_activity = true;
	active_count--;
	if (active_count == 0) {
		sc_time now         = sc_time_stamp();
		active_time_events += (now - last_change);
		active              = false;
		last_change         = now;
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
	active_time_manual    += clk_cycle * active_cycle_fraction;
	active_cycle_fraction  = 0.0;
}

void StatUtilization::dump(std::ostream &os) const {
	sc_time now = sc_time_stamp();

	sc_time active_total;
	sc_time idle_total;

	if (event_activity) {
		sc_time active_ev = active_time_events;
		sc_time idle_ev   = idle_time_events;
		if (active) {
			active_ev += (now - last_change);
		} else {
			idle_ev += (now - last_change);
		}

		active_total = std::min(active_ev + active_time_manual, now);
		idle_total   = idle_ev + idle_time_manual;
	} else {
		active_total     = std::min(active_time_manual, now);
		sc_time idle_est = now - active_total - idle_time_manual;
		if (idle_est < SC_ZERO_TIME) {
			idle_est = SC_ZERO_TIME;
		}
		idle_total = idle_time_manual + idle_est;
	}

	sc_time total       = active_total + idle_total;
	double  utilization = (total > SC_ZERO_TIME) ? active_total.to_seconds() / total.to_seconds() : 0.0;

	os << "{ "
	   << "\"percentage\": " << utilization * 100.0 << ", "
	   << "\"active_time_us\": " << active_total.to_seconds() * 1e6 << ", "
	   << "\"idle_time_us\": " << idle_total.to_seconds() * 1e6 << " }";
}

// -------------------------------------------------------
// StatManager Implementation
// -------------------------------------------------------
namespace {
// Resolve (creating if absent) the stat slot to a typed handle.
template <typename T, typename Map, typename... Args>
T *resolve_stat(Map &module_stats, const std::string &module, const std::string &name, Args &&...args) {
	auto &slot = module_stats[module][name];
	if (!slot) {
		slot = std::make_unique<T>(std::forward<Args>(args)...);
	}
	auto *typed = dynamic_cast<T *>(slot.get());
	if (!typed) {
		LOG_ERROR("StatManager: stat '" + module + "." + name + "' used with a conflicting type");
	}
	return typed;
}
} // namespace

StatValue *StatManager::register_value(const std::string &module, const std::string &name) {
	return resolve_stat<StatValue>(module_stats_, module, name);
}

StatCounter *StatManager::register_counter(const std::string &module, const std::string &name) {
	return resolve_stat<StatCounter>(module_stats_, module, name);
}

StatAccum *StatManager::register_accum(const std::string &module, const std::string &name) {
	return resolve_stat<StatAccum>(module_stats_, module, name);
}

StatMinMax *StatManager::register_minmax(const std::string &module, const std::string &name) {
	return resolve_stat<StatMinMax>(module_stats_, module, name);
}

StatUsage *StatManager::register_usage(const std::string &module, const std::string &name) {
	return resolve_stat<StatUsage>(module_stats_, module, name);
}

StatUtilization *StatManager::register_utilization(const std::string &module, const sc_time clk_cycle) {
	return resolve_stat<StatUtilization>(module_stats_, module, "utilization", clk_cycle);
}

StatUtilization *StatManager::register_utilization(const std::string &module, const std::string &name,
                                                   const sc_time clk_cycle) {
	return resolve_stat<StatUtilization>(module_stats_, module, name, clk_cycle);
}

void StatManager::set_value(const std::string &module, const std::string &name, double value) {
	register_value(module, name)->set(value);
}

void StatManager::increment_counter(const std::string &module, const std::string &name, uint64_t value) {
	register_counter(module, name)->increment(value);
}

void StatManager::update_accum(const std::string &module, const std::string &name, double value) {
	register_accum(module, name)->update(value);
}

void StatManager::update_minmax(const std::string &module, const std::string &name, double value) {
	register_minmax(module, name)->update(value);
}

void StatManager::update_usage(const std::string &module, const std::string &name, unsigned value) {
	register_usage(module, name)->update(value);
}

namespace {
// Look up a registered utilization stat (throws if missing/mismatched).
template <typename Map>
StatUtilization *get_utilization(Map &module_stats, const std::string &module, const std::string &name) {
	auto mit = module_stats.find(module);
	if (mit != module_stats.end()) {
		auto nit = mit->second.find(name);
		if (nit != mit->second.end()) {
			if (auto *util = dynamic_cast<StatUtilization *>(nit->second.get())) {
				return util;
			}
		}
	}
	LOG_ERROR("StatManager: utilization stat '" + module + "." + name + "' not registered");
	return nullptr; // unreachable: LOG_ERROR throws
}
} // namespace

void StatManager::set_active(const std::string &module) {
	get_utilization(module_stats_, module, "utilization")->set_active();
}

void StatManager::set_active(const std::string &module, const std::string &name) {
	get_utilization(module_stats_, module, name)->set_active();
}

void StatManager::set_idle(const std::string &module) {
	get_utilization(module_stats_, module, "utilization")->set_idle();
}

void StatManager::set_idle(const std::string &module, const std::string &name) {
	get_utilization(module_stats_, module, name)->set_idle();
}

void StatManager::add_active_time(const std::string &module, const sc_time delta) {
	get_utilization(module_stats_, module, "utilization")->add_active_time(delta);
}

void StatManager::add_active_time(const std::string &module, const std::string &name, const sc_time delta) {
	get_utilization(module_stats_, module, name)->add_active_time(delta);
}

void StatManager::add_idle_time(const std::string &module, const sc_time delta) {
	get_utilization(module_stats_, module, "utilization")->add_idle_time(delta);
}

void StatManager::add_idle_time(const std::string &module, const std::string &name, const sc_time delta) {
	get_utilization(module_stats_, module, name)->add_idle_time(delta);
}

void StatManager::mark_active_cycle(const std::string &module, const double fraction) {
	get_utilization(module_stats_, module, "utilization")->mark_active_cycle(fraction);
}

void StatManager::mark_active_cycle(const std::string &module, const std::string &name, const double fraction) {
	get_utilization(module_stats_, module, name)->mark_active_cycle(fraction);
}

void StatManager::end_cycle(const std::string &module) {
	get_utilization(module_stats_, module, "utilization")->end_cycle();
}

void StatManager::end_cycle(const std::string &module, const std::string &name) {
	get_utilization(module_stats_, module, name)->end_cycle();
}

void StatManager::start_simulation_timer() {
	time_wall_start_ = high_resolution_clock::now();
	time_sim_start_  = sc_time_stamp();
}

void StatManager::end_simulation_timer() {
	time_wall_value_ = duration<double>(high_resolution_clock::now() - time_wall_start_).count() * 1e3;
	time_sim_value_  = (sc_time_stamp() - time_sim_start_).to_seconds() * 1e6;
}

void StatManager::dump_to_file(const std::string &filename) {
	std::ofstream ofs(filename);
	ofs << "{\n";
	ofs << "  \"execution_time_ms\": " << time_wall_value_ << ",\n";
	ofs << "  \"simulation_time_us\": " << time_sim_value_ << ",\n";
	ofs << "  \"modules\": {\n";

	// Sort module and stat names so the emitted JSON is deterministic regardless
	// of the underlying hash-map ordering.
	std::vector<std::string> module_names;
	module_names.reserve(module_stats_.size());
	for (const auto &kv : module_stats_) {
		module_names.push_back(kv.first);
	}
	std::sort(module_names.begin(), module_names.end());

	bool first_module = true;
	for (const auto &module_name : module_names) {
		const auto &stats = module_stats_.at(module_name);
		if (!first_module) {
			ofs << ",\n";
		}
		first_module = false;
		ofs << "    \"" << module_name << "\": {\n";

		std::vector<std::string> stat_names;
		stat_names.reserve(stats.size());
		for (const auto &kv : stats) {
			stat_names.push_back(kv.first);
		}
		std::sort(stat_names.begin(), stat_names.end());

		bool first_stat = true;
		for (const auto &stat_name : stat_names) {
			const auto &stat = stats.at(stat_name);
			if (!stat) {
				continue; // defensive: skip any empty slot
			}
			if (!first_stat) {
				ofs << ",\n";
			}
			first_stat = false;
			ofs << "      \"" << stat_name << "\": ";
			stat->dump(ofs);
		}

		ofs << "\n    }";
	}

	ofs << "\n  }\n}\n";
}