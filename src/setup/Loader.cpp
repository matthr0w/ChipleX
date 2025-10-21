#include "setup/Loader.h"

#include <dlfcn.h>
#include <filesystem>

#include "globals.h"
#include "logging.h"

void SetupLoader::load_config(const std::string &system_yaml) {
  YAML::Node root = YAML::LoadFile(system_yaml);

  // Assertions
  LOG_ASSERT(root["chiplets"],
             "SetupLoader: Missing required section 'chiplets' in system "
             "description");
  LOG_ASSERT(root["interconnect"],
             "SetupLoader: Missing required section 'interconnect' in system "
             "description");
  LOG_ASSERT(
      root["interconnect"]["type"],
      "SetupLoader: Missing required key 'type' in 'interconnect' section");
  LOG_ASSERT(root["interconnect"]["preset"] ||
                 root["interconnect"]["connections"],
             "SetupLoader: Missing required "
             "section 'connections' in "
             "'interconnect' section");

  // -------------------------------------------------------
  // Chiplets
  // -------------------------------------------------------
  for (const auto &c : root["chiplets"]) {
    LOG_ASSERT(
        c["name"],
        "SetupLoader: Missing required key 'name' in chiplet description");
    LOG_ASSERT(
        c["type"],
        "SetupLoader: Missing required key 'type' in chiplet description");

    std::string name_str = c["name"].as<std::string>();
    std::string type_str = c["type"].as<std::string>();

    LOG_ASSERT(chiplets_defaults_[type_str],
               "SetupLoader: Unknown chiplet type: " + type_str);

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

    sysconf_.chiplets[name_str] = chiplet;
    sysconf_.chiplet_order.push_back(name_str);
  }

  // -------------------------------------------------------
  // Interconnect
  // -------------------------------------------------------
  std::string type_str = root["interconnect"]["type"].as<std::string>();
  std::string interconnect_yaml = interconnects_path_ + type_str + ".yaml";

  LOG_ASSERT(std::filesystem::exists(interconnect_yaml) &&
                 std::filesystem::is_regular_file(interconnect_yaml),
             "SetupLoader: Unknown interconnect type: " + type_str);

  YAML::Node interconnect_defaults = YAML::LoadFile(interconnect_yaml);

  InterconnectType type(InterconnectType::parse(type_str));
  InterconnectConfig interconnect;
  interconnect.type = type;

  // Merge overrides
  if (root["interconnect"]["config"]) {
    for (auto kv : root["interconnect"]["config"]) {
      std::string key = kv.first.as<std::string>();
      merge_nodes(interconnect_defaults[key], kv.second, key);
    }
  }

  interconnect.config = YAML::Clone(interconnect_defaults);

  // Build connections array
  YAML::Node connections_node;
  if (root["interconnect"]["preset"]) {
    ConnectionPreset preset(ConnectionPreset::parse(
        root["interconnect"]["preset"].as<std::string>()));
    connections_node = generate_preset_connections(
        preset, sysconf_.chiplets, root["interconnect"]["connections"]);
  } else {
    connections_node = root["interconnect"]["connections"];
  }

  std::set<std::pair<std::string, std::string>> unique_connections;

  // Add connections to chiplets and create mapping
  for (const auto &conn : connections_node) {
    LOG_ASSERT(conn["endpoints"] && conn["endpoints"].size() == 2,
               "SetupLoader: Missing required key 'endpoints' in connection "
               "description");

    std::string endpoint0 = conn["endpoints"][0].as<std::string>();
    std::string endpoint1 = conn["endpoints"][1].as<std::string>();

    if (sysconf_.chiplets.find(endpoint0) == sysconf_.chiplets.end() ||
        sysconf_.chiplets.find(endpoint1) == sysconf_.chiplets.end()) {
      LOG_WARN("SetupLoader: Invalid connection: " + endpoint0 + " <-> " +
               endpoint1 + ". Ignoring.");
      continue;
    }

    auto pair_norm = std::make_pair(std::min(endpoint0, endpoint1),
                                    std::max(endpoint0, endpoint1));
    if (unique_connections.find(pair_norm) != unique_connections.end()) {
      LOG_WARN("SetupLoader: Duplicate connection: " + endpoint0 + " <-> " +
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
    int idx0 = sysconf_.chiplets[endpoint0].connections.size();
    sysconf_.chiplets[endpoint0].connections.push_back(
        {type, interconnect_config,
         conn["wire_length_mm"] ? conn["wire_length_mm"].as<double>()
                                : wire_length_mm,
         conn["ber_scalar"] ? conn["ber_scalar"].as<double>() : 1.0});

    int idx1 = sysconf_.chiplets[endpoint1].connections.size();
    sysconf_.chiplets[endpoint1].connections.push_back(
        {type, interconnect_config,
         conn["wire_length_mm"] ? conn["wire_length_mm"].as<double>()
                                : wire_length_mm,
         conn["ber_scalar"] ? conn["ber_scalar"].as<double>() : 1.0});

    // Mapping
    auto it0 = std::find(sysconf_.chiplet_order.begin(),
                         sysconf_.chiplet_order.end(), endpoint0);
    auto id0 = std::distance(sysconf_.chiplet_order.begin(), it0);
    auto it1 = std::find(sysconf_.chiplet_order.begin(),
                         sysconf_.chiplet_order.end(), endpoint1);
    auto id1 = std::distance(sysconf_.chiplet_order.begin(), it1);

    interconnect.connections.push_back(
        {{endpoint0, static_cast<int>(id0), idx0},
         {endpoint1, static_cast<int>(id1), idx1}});
  }

  sysconf_.interconnect = interconnect;
}

void SetupLoader::load_code(const std::string &setup_path) {
  LOG_INFO("LOADCODE");
  std::string lib_path = setup_path + "/libsetup.so";
  LOG_INFO("Loading setup library: " << lib_path);

  void *handle = dlopen(lib_path.c_str(), RTLD_LAZY);
  LOG_ASSERT(handle,
             "Failed to open setup library: " + lib_path + "\n" + dlerror());

  using GetCodeFn = CoreCodeMap *(*)();
  auto get_code = reinterpret_cast<GetCodeFn>(dlsym(handle, "get_setup_code"));
  LOG_ASSERT(get_code, "Failed to find symbol get_setup_code in " + lib_path);

  codemap_ = *get_code();
}

YAML::Node SetupLoader::generate_preset_connections(
    const ConnectionPreset &preset,
    const std::map<std::string, ChipletConfig> &chiplets,
    const YAML::Node &overrides) {
  YAML::Node connections_node(YAML::NodeType::Sequence);

  switch (preset.type) {
  case ConnectionPreset::Type::Mesh: {
    int n = sysconf_.chiplet_order.size();
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
          conn["endpoints"][0] = sysconf_.chiplet_order[idx];
          conn["endpoints"][1] = sysconf_.chiplet_order[idx + 1];
          conn["wire_length_mm"] = wire_length_mm;
          conn["ber_scalar"] = 1.0;
          connections_node.push_back(conn);
        }
        // Down neighbor
        if (r + 1 < rows) {
          int idx_down = (r + 1) * cols + c;
          if (idx_down < n) {
            YAML::Node conn;
            conn["endpoints"][0] = sysconf_.chiplet_order[idx];
            conn["endpoints"][1] = sysconf_.chiplet_order[idx_down];
            conn["wire_length_mm"] = wire_length_mm;
            conn["ber_scalar"] = 1.0;
            connections_node.push_back(conn);
          }
        }
      }
    }
    break;
  }

  case ConnectionPreset::Type::Ring: {
    int n = sysconf_.chiplet_order.size();
    for (int i = 0; i < n; i++) {
      YAML::Node conn;
      conn["endpoints"][0] = sysconf_.chiplet_order[i];
      conn["endpoints"][1] = sysconf_.chiplet_order[(i + 1) % n];
      conn["wire_length_mm"] = wire_length_mm;
      conn["ber_scalar"] = 1.0;
      connections_node.push_back(conn);
    }
    break;
  }

  case ConnectionPreset::Type::Star: {
    int n = sysconf_.chiplet_order.size();
    if (n > 1) {
      const std::string &center = sysconf_.chiplet_order[0];
      for (int i = 1; i < n; i++) {
        YAML::Node conn;
        conn["endpoints"][0] = center;
        conn["endpoints"][1] = sysconf_.chiplet_order[i];
        conn["wire_length_mm"] = wire_length_mm;
        conn["ber_scalar"] = 1.0;
        connections_node.push_back(conn);
      }
    }
    break;
  }

  default:
    LOG_WARN("SetupLoader: Unknown preset: " + preset.to_string() +
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
          if (override_conn["ber_scalar"])
            preset_conn["ber_scalar"] = override_conn["ber_scalar"];
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

      if (!matched)
        LOG_WARN("SetupLoader: Invalid connection override: " + endpoint0 +
                 " <-> " + endpoint1 + ". Ignoring.");
    }
  }

  return connections_node;
}

void SetupLoader::merge_nodes(YAML::Node target, const YAML::Node &override,
                              const std::string &path) {
  if (!override)
    return;

  if (override.IsScalar() || override.IsSequence()) {
    if (!target || target.Type() != override.Type())
      LOG_WARN("SetupLoader: Unknown parameter: " + path + ". Ignoring.");
    else
      target = override;
  } else if (override.IsMap()) {
    for (auto kv : override) {
      const std::string key = kv.first.as<std::string>();
      std::string full_path = path.empty() ? key : path + "." + key;

      if (target[key])
        merge_nodes(target[key], kv.second, full_path);
      else
        LOG_WARN("SetupLoader: Unknown parameter: " + full_path +
                 ". Ignoring.");
    }
  }
}