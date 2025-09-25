#include "system.h"

#include <filesystem>

#include "globals.h"
#include "logging.h"

void SystemLoader::load(const std::string &system_yaml) {
  YAML::Node root = YAML::LoadFile(system_yaml);

  if (!root["chiplets"])
    LOG_ERROR("SystemLoader: Missing required section 'chiplets' in system "
              "description.");
  if (!root["interconnect"])
    LOG_ERROR("SystemLoader: Missing required section 'interconnect' in system "
              "description.");
  if (!root["interconnect"]["type"])
    LOG_ERROR(
        "SystemLoader: Missing required key 'type' in 'interconnect' section.");
  if (!root["interconnect"]["preset"] && !root["interconnect"]["connections"])
    LOG_ERROR("SystemLoader: Missing required section 'connections' in "
              "'interconnect' section.");

  // -------------------------------------------------------
  // Chiplets
  // -------------------------------------------------------
  for (const auto &c : root["chiplets"]) {
    if (!c["name"])
      LOG_ERROR(
          "SystemLoader: Missing required key 'name' in chiplet description.");
    if (!c["type"])
      LOG_ERROR(
          "SystemLoader: Missing required key 'type' in chiplet description.");

    std::string name_str = c["name"].as<std::string>();
    std::string type_str = c["type"].as<std::string>();

    if (!chiplets_defaults_[type_str])
      LOG_ERROR("SystemLoader: Unknown chiplet type: " + type_str);

    YAML::Node chiplet_config = YAML::Clone(chiplets_defaults_[type_str]);

    // Merge overrides
    if (c["config"]) {
      for (auto it : c["config"]) {
        std::string section = it.first.as<std::string>();
        merge_nodes(chiplet_config[section], it.second);
      }
    }

    ChipletType type(ChipletType::parse(type_str));
    ChipletConfig chiplet{type, chiplet_config, {}};

    system_.chiplets[name_str] = chiplet;
    system_.chiplet_order.push_back(name_str);
  }

  // -------------------------------------------------------
  // Interconnect
  // -------------------------------------------------------
  std::string type_str = root["interconnect"]["type"].as<std::string>();
  std::string interconnect_yaml = interconnects_path_ + type_str + ".yaml";

  if (!(std::filesystem::exists(interconnect_yaml) &&
        std::filesystem::is_regular_file(interconnect_yaml)))
    LOG_ERROR("SystemLoader: Unknown interconnect type: " + type_str);

  YAML::Node interconnect_defaults = YAML::LoadFile(interconnect_yaml);

  InterconnectType type(InterconnectType::parse(type_str));
  InterconnectConfig interconnect;
  interconnect.type = type;
  interconnect.defaults = YAML::Clone(interconnect_defaults);

  // Build connections array
  YAML::Node connections_node;
  if (root["interconnect"]["preset"]) {
    ConnectionPreset preset(ConnectionPreset::parse(
        root["interconnect"]["preset"].as<std::string>()));
    connections_node = generate_preset_connections(
        preset, system_.chiplets, root["interconnect"]["connections"]);
  } else {
    connections_node = root["interconnect"]["connections"];
  }

  std::set<std::pair<std::string, std::string>> unique_connections;

  // Add connections to chiplets and create mapping
  for (const auto &conn : connections_node) {
    if (!conn["endpoints"] || conn["endpoints"].size() != 2)
      LOG_ERROR("SystemLoader: Missing required key 'endpoints' in connection "
                "description.");

    std::string endpoint0 = conn["endpoints"][0].as<std::string>();
    std::string endpoint1 = conn["endpoints"][1].as<std::string>();

    if (system_.chiplets.find(endpoint0) == system_.chiplets.end() ||
        system_.chiplets.find(endpoint1) == system_.chiplets.end()) {
      LOG_WARN("SystemLoader: Invalid connection: " + endpoint0 + " <-> " +
               endpoint1 + ". Ignoring.");
      continue;
    }

    auto pair_norm = std::make_pair(std::min(endpoint0, endpoint1),
                                    std::max(endpoint0, endpoint1));
    if (unique_connections.find(pair_norm) != unique_connections.end()) {
      LOG_WARN("SystemLoader: Duplicate connection: " + endpoint0 + " <-> " +
               endpoint1 + ". Ignoring.");
      continue;
    }

    unique_connections.insert(pair_norm);

    YAML::Node interconnect_config = YAML::Clone(interconnect_defaults);

    // Merge overrides
    if (conn["config"]) {
      for (auto kv : conn["config"]) {
        std::string key = kv.first.as<std::string>();
        merge_nodes(interconnect_config[key], kv.second, key);
      }
    }

    // Assign to both chiplets (bidirectional symmetry)
    unsigned idx0 = system_.chiplets[endpoint0].connections.size();
    system_.chiplets[endpoint0].connections.push_back(
        {type, interconnect_config,
         conn["wire_length_mm"] ? conn["wire_length_mm"].as<double>()
                                : wire_length_mm});

    unsigned idx1 = system_.chiplets[endpoint1].connections.size();
    system_.chiplets[endpoint1].connections.push_back(
        {type, interconnect_config,
         conn["wire_length_mm"] ? conn["wire_length_mm"].as<double>()
                                : wire_length_mm});

    // Mapping
    interconnect.connections.push_back({{endpoint0, idx0}, {endpoint1, idx1}});
  }

  system_.interconnect = interconnect;
}

YAML::Node SystemLoader::generate_preset_connections(
    const ConnectionPreset &preset,
    const std::map<std::string, ChipletConfig> &chiplets,
    const YAML::Node &overrides) {
  YAML::Node connections_node(YAML::NodeType::Sequence);

  switch (preset.type) {
  case ConnectionPreset::Type::Mesh: {
    int n = system_.chiplet_order.size();
    int rows = static_cast<int>(std::sqrt(n));
    while (rows * rows < n)
      rows++;
    int cols = (n + rows - 1) / rows;

    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        int idx = r * cols + c;
        if (idx >= n)
          continue;

        // Right neighbor
        if (c + 1 < cols && (idx + 1) < n) {
          YAML::Node conn;
          conn["endpoints"][0] = system_.chiplet_order[idx];
          conn["endpoints"][1] = system_.chiplet_order[idx + 1];
          conn["wire_length_mm"] = wire_length_mm;
          connections_node.push_back(conn);
        }
        // Down neighbor
        if (r + 1 < rows) {
          int idx_down = (r + 1) * cols + c;
          if (idx_down < n) {
            YAML::Node conn;
            conn["endpoints"][0] = system_.chiplet_order[idx];
            conn["endpoints"][1] = system_.chiplet_order[idx_down];
            conn["wire_length_mm"] = wire_length_mm;
            connections_node.push_back(conn);
          }
        }
      }
    }
    break;
  }

  case ConnectionPreset::Type::Ring: {
    int n = system_.chiplet_order.size();
    for (int i = 0; i < n; i++) {
      YAML::Node conn;
      conn["endpoints"][0] = system_.chiplet_order[i];
      conn["endpoints"][1] = system_.chiplet_order[(i + 1) % n];
      conn["wire_length_mm"] = wire_length_mm;
      connections_node.push_back(conn);
    }
    break;
  }

  case ConnectionPreset::Type::Star: {
    int n = system_.chiplet_order.size();
    if (n > 1) {
      const std::string &center = system_.chiplet_order[0];
      for (int i = 1; i < n; i++) {
        YAML::Node conn;
        conn["endpoints"][0] = center;
        conn["endpoints"][1] = system_.chiplet_order[i];
        conn["wire_length_mm"] = wire_length_mm;
        connections_node.push_back(conn);
      }
    }
    break;
  }

  default:
    LOG_WARN("SystemLoader: Unknown preset: " + preset.to_string() +
             ". Ignoring.");
  }

  // Apply connection overrides
  if (overrides) {
    for (const auto &override_conn : overrides) {
      std::string endpoint0 = override_conn["endpoints"][0].as<std::string>();
      std::string endpoint1 = override_conn["endpoints"][1].as<std::string>();
      auto norm = std::minmax(endpoint0, endpoint1);

      bool matched = false;
      for (YAML::Node preset_conn : connections_node) {
        auto norm2 = std::minmax(preset_conn["endpoints"][0].as<std::string>(),
                                 preset_conn["endpoints"][1].as<std::string>());

        if (norm == norm2) {
          if (override_conn["wire_length_mm"])
            preset_conn["wire_length_mm"] = override_conn["wire_length_mm"];
          // Merge overrides
          if (override_conn["config"]) {
            for (auto kv : override_conn["config"]) {
              std::string key = kv.first.as<std::string>();
              preset_conn["config"][key] = kv.second;
            }
          }
          matched = true;
          break;
        }
      }

      if (!matched) {
        LOG_WARN("SystemLoader: Invalid connection override: " + endpoint0 +
                 " <-> " + endpoint1 + ". Ignoring.");
      }
    }
  }

  return connections_node;
}

void SystemLoader::merge_nodes(YAML::Node target, const YAML::Node &override,
                               const std::string &path) {
  if (!override)
    return;

  if (override.IsScalar() || override.IsSequence()) {
    if (!target || target.Type() != override.Type())
      LOG_WARN("SystemLoader: Unknown parameter: " + path + ". Ignoring.");
    else
      target = override;
  } else if (override.IsMap()) {
    for (auto kv : override) {
      const std::string key = kv.first.as<std::string>();
      std::string full_path = path.empty() ? key : path + "." + key;

      if (target[key])
        merge_nodes(target[key], kv.second, full_path);
      else
        LOG_WARN("SystemLoader: Unknown parameter: " + full_path +
                 ". Ignoring.");
    }
  }
}

static void print_yaml_node(const YAML::Node &node, int indent = 0) {
  std::string pad(indent, ' ');
  if (!node) {
    std::cout << pad << "(null)\n";
    return;
  }

  if (node.IsScalar()) {
    std::cout << pad << node.as<std::string>() << "\n";
  } else if (node.IsSequence()) {
    for (auto v : node) {
      print_yaml_node(v, indent + 2);
    }
  } else if (node.IsMap()) {
    for (auto kv : node) {
      std::cout << pad << kv.first.as<std::string>() << ":\n";
      print_yaml_node(kv.second, indent + 2);
    }
  }
}

void SystemLoader::print_system_config() {
  std::cout << "================ SYSTEM CONFIG ================\n";

  // -------------------------------------------------------
  // Chiplets
  // -------------------------------------------------------
  std::cout << "Chiplets:\n";
  for (const auto &name : system_.chiplet_order) {
    const ChipletConfig &chip = system_.chiplets.at(name);
    std::cout << "  Chiplet: " << name << " (type=" << chip.type.to_string()
              << ")\n";

    std::cout << "    Config:\n";
    print_yaml_node(chip.config, 6);

    std::cout << "    Connections:\n";
    for (size_t i = 0; i < chip.connections.size(); ++i) {
      const auto &conn_cfg = chip.connections[i];
      std::cout << "      [" << i << "] type=" << conn_cfg.type.to_string()
                << ", wire_length=" << conn_cfg.wire_length << "mm\n";
      print_yaml_node(conn_cfg.config, 8);
    }
  }

  // -------------------------------------------------------
  // Interconnect
  // -------------------------------------------------------
  std::cout << "Interconnect:\n";
  std::cout << "  Type: " << system_.interconnect.type.to_string() << "\n";
  std::cout << "  Defaults:\n";
  print_yaml_node(system_.interconnect.defaults, 4);

  std::cout << "  Mappings:\n";
  for (size_t i = 0; i < system_.interconnect.connections.size(); ++i) {
    const auto &m = system_.interconnect.connections[i];
    std::cout << "    [" << i << "] " << m.endpoint0.chiplet << ".conn["
              << m.endpoint0.index << "] <-> " << m.endpoint1.chiplet
              << ".conn[" << m.endpoint1.index << "]\n";
  }

  std::cout << "===============================================\n";
}