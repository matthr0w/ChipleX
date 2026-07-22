#include "setup/Loader.h"

#include <dlfcn.h>
#include <filesystem>

#include "logging.h"

void SetupLoader::load_system_config(const std::string &system_file) {
	YAML::Node root = YAML::LoadFile(system_file);

	// Assertions
	LOG_ASSERT(root["chiplets"], "SetupLoader: Missing required section 'chiplets' in system description");
	LOG_ASSERT(root["connections"], "SetupLoader: Missing required section 'connections' in system description");

	// -------------------------------------------------------
	// Chiplets
	// -------------------------------------------------------
	unsigned chiplet_id = 0;
	for (const auto &c : root["chiplets"]) {
		LOG_ASSERT(c["name"], "SetupLoader: Missing required key 'name' in chiplet description");
		LOG_ASSERT(c["type"], "SetupLoader: Missing required key 'type' in chiplet description");
		LOG_ASSERT(c["interconnects"], "SetupLoader: Missing required section 'interconnects' in chiplet description");

		std::string chiplet_name = c["name"].as<std::string>();
		std::string chiplet_type = c["type"].as<std::string>();
		std::string chiplet_file = chiplets_path_ + chiplet_type + ".yaml";

		LOG_ASSERT(std::filesystem::exists(chiplet_file) && std::filesystem::is_regular_file(chiplet_file),
		           "SetupLoader: Unknown chiplet type: " + chiplet_type);

		YAML::Node chiplet_config = YAML::LoadFile(chiplet_file);

		// Merge overrides
		if (c["config"]) {
			for (auto it : c["config"]) {
				std::string section = it.first.as<std::string>();
				merge_nodes(chiplet_config[section], it.second);
			}
		}

		ChipletType   type(ChipletType::parse(chiplet_type));
		ChipletConfig chiplet{type, chiplet_config};

		// -------------------------------------------------------
		// Accelerators
		// -------------------------------------------------------
		for (const auto &ic : c["accelerators"]) {
			LOG_ASSERT(ic["name"], "SetupLoader: Missing required key 'name' in accelerator description");
			LOG_ASSERT(ic["type"], "SetupLoader: Missing required key 'type' in accelerator description");

			std::string accel_name = ic["name"].as<std::string>();
			std::string accel_type = ic["type"].as<std::string>();
			std::string accel_file = accels_path_ + accel_type + ".yaml";

			LOG_ASSERT(std::filesystem::exists(accel_file) && std::filesystem::is_regular_file(accel_file),
			           "SetupLoader: Unknown accelerator type: " + accel_type);

			YAML::Node accel_config = YAML::LoadFile(accel_file);

			// Merge overrides
			if (ic["config"]) {
				for (auto it : ic["config"]) {
					std::string section = it.first.as<std::string>();
					merge_nodes(accel_config[section], it.second);
				}
			}

			AccelType   type(AccelType::parse(accel_type));
			AccelConfig accel{type, accel_config};
			chiplet.accels[accel_name] = accel;
		}

		// -------------------------------------------------------
		// Interconnects
		// -------------------------------------------------------
		unsigned interconnect_id = 0;
		for (const auto &ic : c["interconnects"]) {
			LOG_ASSERT(ic["name"], "SetupLoader: Missing required key 'name' in interconnect description");
			LOG_ASSERT(ic["type"], "SetupLoader: Missing required key 'type' in interconnect description");

			std::string interconnect_name = ic["name"].as<std::string>();
			std::string interconnect_type = ic["type"].as<std::string>();
			std::string interconnect_file = interconnects_path_ + interconnect_type + ".yaml";

			LOG_ASSERT(std::filesystem::exists(interconnect_file) &&
			               std::filesystem::is_regular_file(interconnect_file),
			           "SetupLoader: Unknown interconnect type: " + interconnect_type);

			YAML::Node interconnect_config = YAML::LoadFile(interconnect_file);

			// Merge overrides
			if (ic["config"]) {
				for (auto it : ic["config"]) {
					std::string section = it.first.as<std::string>();
					merge_nodes(interconnect_config[section], it.second);
				}
			}

			InterconnectType   type(InterconnectType::parse(interconnect_type));
			InterconnectConfig interconnect{type, interconnect_config};
			chiplet.interconnects[interconnect_name]            = interconnect;
			chiplet.interconnect_ids[interconnect_name]         = interconnect_id;
			chiplet.interconnect_ids_reverse[interconnect_id++] = interconnect_name;
		}

		sysconf_.chiplets[chiplet_name]    = chiplet;
		sysconf_.chiplet_ids[chiplet_name] = chiplet_id++;
	}

	// -------------------------------------------------------
	// Connections
	// -------------------------------------------------------
	std::set<std::pair<std::string, std::string>> unique_connections;

	// Add connection configs to interconnects and create connection mappings
	for (const auto &conn : root["connections"]) {
		LOG_ASSERT(conn["endpoints"] && conn["endpoints"].size() == 2,
		           "SetupLoader: Missing required key 'endpoints' in connection description");

		std::string endpoint0 = conn["endpoints"][0].as<std::string>();
		std::string endpoint1 = conn["endpoints"][1].as<std::string>();

		// Split endpoints: "chiplet.interconnect"
		auto split_endpoint = [](const std::string &endpoint) -> std::pair<std::string, std::string> {
			auto pos = endpoint.find('.');
			if (pos == std::string::npos) {
				return {endpoint, ""}; // Invalid format
			}
			return {endpoint.substr(0, pos), endpoint.substr(pos + 1)};
		};

		auto [chiplet0_name, interconnect0_name] = split_endpoint(endpoint0);
		auto [chiplet1_name, interconnect1_name] = split_endpoint(endpoint1);

		// Check that chiplets exist
		auto chiplet0_it = sysconf_.chiplets.find(chiplet0_name);
		auto chiplet1_it = sysconf_.chiplets.find(chiplet1_name);

		if (chiplet0_it == sysconf_.chiplets.end() || chiplet1_it == sysconf_.chiplets.end()) {
			LOG_WARN("SetupLoader: Invalid chiplet(s) in connection: " + endpoint0 + " <-> " + endpoint1 +
			         ". Ignoring.");
			continue;
		}

		ChipletConfig &chiplet0 = chiplet0_it->second;
		ChipletConfig &chiplet1 = chiplet1_it->second;

		// Check that interconnects exist
		auto interconnect0_it = chiplet0.interconnects.find(interconnect0_name);
		auto interconnect1_it = chiplet1.interconnects.find(interconnect1_name);

		if (interconnect0_it == chiplet0.interconnects.end() || interconnect1_it == chiplet1.interconnects.end()) {
			LOG_WARN("SetupLoader: Invalid interconnect(s) in connection: " + endpoint0 + " <-> " + endpoint1 +
			         ". Ignoring.");
			continue;
		}

		InterconnectConfig &interconnect0 = interconnect0_it->second;
		InterconnectConfig &interconnect1 = interconnect1_it->second;

		if (interconnect0.type.value != interconnect1.type.value) {
			LOG_WARN("SetupLoader: Not matching interconnect types in connection: " + endpoint0 + " <-> " + endpoint1 +
			         ". Ignoring.");
			continue;
		}

		auto pair_norm = std::make_pair(std::min(endpoint0, endpoint1), std::max(endpoint0, endpoint1));
		if (unique_connections.find(pair_norm) != unique_connections.end()) {
			LOG_WARN("SetupLoader: Duplicate connection: " + endpoint0 + " <-> " + endpoint1 + ". Ignoring.");
			continue;
		}

		unique_connections.insert(pair_norm);

		YAML::Node connection_config = YAML::Clone(interconnect0.node);

		// Merge overrides
		if (conn["config"]) {
			for (auto kv : conn["config"]) {
				std::string key = kv.first.as<std::string>();
				merge_nodes(connection_config[key], kv.second, key);
			}
		}

		// Add to both interconnects (bidirectional symmetry)
		int link0_id = interconnect0.connections.size();
		interconnect0.connections.push_back(
		    {connection_config, conn["wire_length_mm"] ? conn["wire_length_mm"].as<double>() : wire_length_mm,
		     conn["ber_scalar"] ? conn["ber_scalar"].as<double>() : 1.0});

		int link1_id = interconnect1.connections.size();
		interconnect1.connections.push_back(
		    {connection_config, conn["wire_length_mm"] ? conn["wire_length_mm"].as<double>() : wire_length_mm,
		     conn["ber_scalar"] ? conn["ber_scalar"].as<double>() : 1.0});

		// Mapping
		unsigned chiplet0_id      = sysconf_.chiplet_ids.find(chiplet0_name)->second;
		unsigned chiplet1_id      = sysconf_.chiplet_ids.find(chiplet1_name)->second;
		unsigned interconnect0_id = chiplet0.interconnect_ids.find(interconnect0_name)->second;
		unsigned interconnect1_id = chiplet1.interconnect_ids.find(interconnect1_name)->second;

		sysconf_.connections.push_back({
		    {chiplet0_name, chiplet0_id, interconnect0_name, interconnect0_id, static_cast<unsigned>(link0_id)},
		    {chiplet1_name, chiplet1_id, interconnect1_name, interconnect1_id, static_cast<unsigned>(link1_id)}
        });
	}
}

void SetupLoader::load_cycles_db(const std::string &workloads_file) {
	if (!std::filesystem::exists(workloads_file)) {
		return;
	}

	try {
		YAML::Node root = YAML::LoadFile(workloads_file);
		LOG_ASSERT(root["workloads"], "SetupLoader: Missing required section 'workloads' in workloads database");

		for (const auto &entry : root["workloads"]) {
			std::string       name = entry.first.as<std::string>();
			const YAML::Node &node = entry.second;
			LOG_ASSERT(node["cycles_count"], "SetupLoader: Missing required key 'cycles_count' for " + name);
			sysconf_.cycles.db[name] = node["cycles_count"].as<unsigned>();
		}
	} catch (const YAML::Exception &e) {
		LOG_ERROR("SetupLoader: Failed to load " + workloads_file + ": " + e.what());
	}
}

void SetupLoader::load_lib_code(const std::string &setup_lib) {
	void *handle = dlopen(setup_lib.c_str(), RTLD_LAZY);
	LOG_ASSERT(handle, "SetupLoader: Failed to open " + setup_lib + ": " + dlerror());

	using GetCodeFn = ModuleCodeMap *(*)();
	auto get_code   = reinterpret_cast<GetCodeFn>(dlsym(handle, "get_program_code"));
	LOG_ASSERT(get_code, "SetupLoader: Failed to find symbol get_program_code in " + setup_lib);

	ModuleCodeMap code_map = *get_code();
	for (auto &[key, funcs] : code_map) {
		const auto &chiplet_name = key.first;
		const auto &module_name  = key.second;

		auto it = sysconf_.chiplets.find(chiplet_name);
		if (it == sysconf_.chiplets.end()) {
			LOG_WARN("SetupLoader: Program specifies unknown chiplet " << chiplet_name << ". Ignoring.");
			continue;
		}

		it->second.module_code[module_name] = std::move(funcs);
	}
}

void SetupLoader::merge_nodes(YAML::Node target, const YAML::Node &override, const std::string &path) {
	if (!override) {
		return;
	}

	if (override.IsScalar() || override.IsSequence()) {
		if (!target || target.Type() != override.Type()) {
			LOG_WARN("SetupLoader: Unknown parameter: " + path + ". Ignoring.");
		} else {
			target = override;
		}
	} else if (override.IsMap()) {
		for (auto kv : override) {
			const std::string key       = kv.first.as<std::string>();
			std::string       full_path = path.empty() ? key : path + "." + key;

			if (target[key]) {
				merge_nodes(target[key], kv.second, full_path);
			} else {
				LOG_WARN("SetupLoader: Unknown parameter: " + full_path + ". Ignoring.");
			}
		}
	}
}