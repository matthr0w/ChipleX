#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Module names
const std::string BUS_MODULE_NAME = "bus";
const std::string CACHE_MODULE_NAME = "cache";
const std::string CORE_MODULE_NAME = "core";
const std::string DMA_ENGINE_MODULE_NAME = "dma_engine";
const std::string EXT_LAYER_MODULE_NAME = "ext_layer";
const std::string MEMORY_MODULE_NAME = "memory";

// Module AXI types
enum class AXIModuleType {
  NONE,
  MANAGER,
  SUBORDINATE,
  BUS,
  BUS_MANAGER,
  BUS_SUBORDINATE,
  INTERCONNECT
};

struct ChipletModuleInfo {
  unsigned id;
  std::string name;

  std::vector<AXIModuleType> types; // Multiple types allowed

  std::optional<unsigned> bus_mgr_port;
  std::optional<unsigned> bus_sub_port;

  bool is_manager() const {
    return std::find(types.begin(), types.end(), AXIModuleType::MANAGER) !=
           types.end();
  }
  bool is_subordinate() const {
    return std::find(types.begin(), types.end(), AXIModuleType::SUBORDINATE) !=
           types.end();
  }
  bool is_bus() const {
    return std::find(types.begin(), types.end(), AXIModuleType::BUS) !=
           types.end();
  }
  bool is_bus_manager() const {
    return std::find(types.begin(), types.end(), AXIModuleType::BUS_MANAGER) !=
           types.end();
  }
  bool is_bus_subordinate() const {
    return std::find(types.begin(), types.end(),
                     AXIModuleType::BUS_SUBORDINATE) != types.end();
  }
  bool is_interconnect() const {
    return std::find(types.begin(), types.end(), AXIModuleType::INTERCONNECT) !=
           types.end();
  }
};

struct ChipletDescriptor {
  unsigned chiplet_id;
  std::string chiplet_name;

  std::vector<ChipletModuleInfo> modules;

  unsigned next_module_id = 0;
  unsigned next_mgr_port = 0;
  unsigned next_sub_port = 0;

  unsigned allocate_module_id() { return next_module_id++; }
  unsigned allocate_mgr_port() { return next_mgr_port++; }
  unsigned allocate_sub_port() { return next_sub_port++; }

  ChipletModuleInfo &add_module(const std::string &name,
                                std::initializer_list<AXIModuleType> types) {
    ChipletModuleInfo module;

    module.id = allocate_module_id();
    module.name = name;

    for (AXIModuleType type : types) {
      module.types.push_back(type);
      switch (type) {
      case AXIModuleType::BUS_MANAGER:
        // Bus manager is also general manager
        module.types.push_back(AXIModuleType::MANAGER);
        module.bus_mgr_port = allocate_mgr_port();
        break;
      case AXIModuleType::BUS_SUBORDINATE:
        // Bus subordinate is also general subordinate
        module.types.push_back(AXIModuleType::SUBORDINATE);
        module.bus_sub_port = allocate_sub_port();
        break;
      default:
        break;
      }
    }

    modules.push_back(std::move(module));
    return modules.back();
  }

  // Lookup tables
  std::unordered_map<unsigned, unsigned> id_to_mgr_port;
  std::unordered_map<std::string, unsigned> name_to_mgr_port;
  std::unordered_map<unsigned, unsigned> id_to_sub_port;
  std::unordered_map<std::string, unsigned> name_to_sub_port;
  std::unordered_map<unsigned, std::string> mgr_port_to_name;
  std::unordered_map<unsigned, std::string> sub_port_to_name;

  void generate_luts() {
    id_to_mgr_port.clear();
    name_to_mgr_port.clear();
    id_to_sub_port.clear();
    name_to_sub_port.clear();
    mgr_port_to_name.clear();
    sub_port_to_name.clear();

    // Precompute name -> ChipletModuleInfo * map for fast lookup
    std::unordered_map<std::string, const ChipletModuleInfo *> name_map;
    for (const auto &m : modules)
      name_map[m.name] = &m;

    for (const auto &m : modules) {
      const ChipletModuleInfo *port_module = &m;

      // For interconnects the actual port modules are the extension layers.
      // We register the interconnect and its extension layer with the same bus
      // ports in the LUTs. This is not ideal but allows the user to use the
      // interconnect in the API.
      if (m.is_interconnect()) {
        // Look up the extension layer module
        std::string ext_name = EXT_LAYER_MODULE_NAME + "_" + m.name;
        auto it = name_map.find(ext_name);
        if (it != name_map.end())
          port_module = it->second;
      }

      if (port_module->bus_mgr_port.has_value()) {
        unsigned mgr_port = port_module->bus_mgr_port.value();
        // Map ID -> port
        id_to_mgr_port[m.id] = mgr_port;
        name_to_mgr_port[m.name] = mgr_port;
        // Map port -> name
        mgr_port_to_name[mgr_port] = m.name;
      }

      if (port_module->bus_sub_port.has_value()) {
        unsigned sub_port = port_module->bus_sub_port.value();
        // Map ID -> port
        id_to_sub_port[m.id] = sub_port;
        name_to_sub_port[m.name] = sub_port;
        // Map port -> name
        sub_port_to_name[sub_port] = m.name;
      }
    }
  }

  const ChipletModuleInfo *get(const unsigned id) const {
    for (const auto &m : modules)
      if (m.id == id)
        return &m;
    return nullptr;
  }

  const ChipletModuleInfo *get(const std::string &name) const {
    for (const auto &m : modules)
      if (m.name == name)
        return &m;
    return nullptr;
  }

  unsigned get_mgr_port(unsigned id) const {
    auto it = id_to_mgr_port.find(id);
    return it != id_to_mgr_port.end() ? it->second : 0;
  }

  unsigned get_mgr_port(const std::string &name) const {
    auto it = name_to_mgr_port.find(name);
    return it != name_to_mgr_port.end() ? it->second : 0;
  }

  unsigned get_sub_port(unsigned id) const {
    auto it = id_to_sub_port.find(id);
    return it != id_to_sub_port.end() ? it->second : 0;
  }

  unsigned get_sub_port(const std::string &name) const {
    auto it = name_to_sub_port.find(name);
    return it != name_to_sub_port.end() ? it->second : 0;
  }

  std::string get_name_at_mgr_port(unsigned port) const {
    auto it = mgr_port_to_name.find(port);
    return it != mgr_port_to_name.end() ? it->second : "";
  }

  std::string get_name_at_sub_port(unsigned port) const {
    auto it = sub_port_to_name.find(port);
    return it != sub_port_to_name.end() ? it->second : "";
  }

  size_t num_managers() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_manager(); });
  }

  size_t num_subordinates() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_subordinate(); });
  }

  size_t num_bus() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_bus(); });
  }

  size_t num_bus_managers() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_bus_manager(); });
  }

  size_t num_bus_subordinates() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_bus_subordinate(); });
  }

  size_t num_interconnects() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_interconnect(); });
  }
};