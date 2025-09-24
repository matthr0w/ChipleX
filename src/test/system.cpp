#include "system.h"

#include <filesystem>

#include "logging.h"

void SystemLoader::load(const std::string &system_yaml) {
  YAML::Node root = YAML::LoadFile(system_yaml);

  // Chiplets
  for (const auto &c : root["chiplets"]) {
    std::string name_str = c["name"].as<std::string>();
    std::string type_str = c["type"].as<std::string>();

    if (!chiplets_defaults_[type_str])
      LOG_ERROR("SystemLoader: Unknown chiplet type: " + type_str);

    YAML::Node chiplet_config = YAML::Clone(chiplets_defaults_[type_str]);

    if (c["config"]) {
      for (auto it : c["config"]) {
        std::string section = it.first.as<std::string>();
        merge_nodes(chiplet_config[section], it.second);
      }
    }

    ChipletType type(ChipletType::parse(type_str));
    ChipletConfig chiplet{type, chiplet_config,
                          c["interconnect"] ? c["interconnect"] : YAML::Node()};

    system_.chiplets[name_str] = chiplet;
  }

  // Interconnect
  if (!root["interconnect"])
    LOG_ERROR("SystemLoader: Missing interconnect section");

  std::string type_str = root["interconnect"]["type"].as<std::string>();
  std::string interconnect_yaml = interconnects_path_ + type_str + ".yaml";

  if (!(std::filesystem::exists(interconnect_yaml) &&
        std::filesystem::is_regular_file(interconnect_yaml)))
    LOG_ERROR("SystemLoader: Unknown interconnect type: " + type_str);

  YAML::Node interconnect_defaults = YAML::LoadFile(interconnect_yaml);

  YAML::Node interconnect_config = YAML::Clone(interconnect_defaults);

  for (const auto &[chiplet_name, chiplet] : system_.chiplets) {
    if (chiplet.interconnect_overrides) {
      merge_nodes(interconnect_config, chiplet.interconnect_overrides);
    }
  }

  InterconnectType type(InterconnectType::parse(type_str));
  InterconnectConfig interconnect;
  interconnect.type = type;
  interconnect.config = interconnect_config;

  std::set<std::pair<std::string, std::string>> unique_connections;

  for (const auto &conn : root["interconnect"]["connections"]) {
    std::string from = conn["from"].as<std::string>();
    std::string to = conn["to"].as<std::string>();

    if (system_.chiplets.find(from) == system_.chiplets.end() ||
        system_.chiplets.find(to) == system_.chiplets.end()) {
      LOG_WARN("SystemLoader: Invalid connection: " + from + " -> " + to);
      continue;
    }

    auto pair_norm = std::make_pair(std::min(from, to), std::max(from, to));

    if (unique_connections.find(pair_norm) != unique_connections.end()) {
      LOG_WARN("SystemLoader: Duplicate connection: " + from + " <-> " + to);
      continue;
    }

    unique_connections.insert(pair_norm);

    interconnect.connections.push_back(
        {from, to, conn["distance_um"].as<int>()});
  }

  system_.interconnect = interconnect;
}

void SystemLoader::merge_nodes(YAML::Node target, const YAML::Node &override,
                               const std::string &path) {
  if (!override)
    return;

  if (override.IsScalar() || override.IsSequence()) {
    if (!target || target.Type() != override.Type())
      LOG_WARN("SystemLoader: Unknown parameter: " << path);
    else
      target = override;
  } else if (override.IsMap()) {
    for (auto kv : override) {
      const std::string key = kv.first.as<std::string>();
      std::string full_path = path.empty() ? key : path + "." + key;

      if (target[key])
        merge_nodes(target[key], kv.second, full_path);
      else
        LOG_WARN("SystemLoader: Unknown parameter: " << full_path);
    }
  }
}