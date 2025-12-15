#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

// Module names
const std::string BUS_MODULE_NAME = "bus";
const std::string CACHE_MODULE_NAME = "cache";
const std::string CORE_MODULE_NAME = "core";
const std::string DMA_ENGINE_MODULE_NAME = "dma_engine";
const std::string MEMORY_MODULE_NAME = "memory";

// Module AXI types
enum class AXIModuleType { NONE, BUS, INTERCONNECT, MANAGER, SUBORDINATE };

struct ChipletModuleInfo {
  unsigned id;
  std::string name;

  std::vector<AXIModuleType> types; // Multiple types allowed

  std::optional<unsigned> bus_mgr_port;
  std::optional<unsigned> bus_sub_port;

  unsigned get_mgr_port() const { return bus_mgr_port.value_or(0); }
  unsigned get_sub_port() const { return bus_sub_port.value_or(0); }

  bool is_bus() const {
    return std::find(types.begin(), types.end(), AXIModuleType::BUS) !=
           types.end();
  }
  bool is_interconnect() const {
    return std::find(types.begin(), types.end(), AXIModuleType::INTERCONNECT) !=
           types.end();
  }
  bool is_manager() const {
    return std::find(types.begin(), types.end(), AXIModuleType::MANAGER) !=
           types.end();
  }
  bool is_subordinate() const {
    return std::find(types.begin(), types.end(), AXIModuleType::SUBORDINATE) !=
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
      case AXIModuleType::MANAGER:
        module.bus_mgr_port = allocate_mgr_port();
        break;
      case AXIModuleType::SUBORDINATE:
        module.bus_sub_port = allocate_sub_port();
        break;
      default:
        break;
      }
    }

    modules.push_back(std::move(module));
    return modules.back();
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

  const unsigned get_mgr_port(const unsigned id) const {
    for (const auto &m : modules)
      if (m.id == id)
        return m.bus_mgr_port.value_or(0);
    return 0;
  }

  const unsigned get_mgr_port(const std::string &name) const {
    for (const auto &m : modules)
      if (m.name == name)
        return m.bus_mgr_port.value_or(0);
    return 0;
  }

  const unsigned get_sub_port(const unsigned id) const {
    for (const auto &m : modules)
      if (m.id == id)
        return m.bus_sub_port.value_or(0);
    return 0;
  }

  const unsigned get_sub_port(const std::string &name) const {
    for (const auto &m : modules)
      if (m.name == name)
        return m.bus_sub_port.value_or(0);
    return 0;
  }

  const std::string get_name_at_mgr_port(const unsigned port) const {
    for (const auto &m : modules)
      if (m.bus_mgr_port == port)
        return m.name;
    return nullptr;
  }

  const std::string get_name_at_sub_port(const unsigned port) const {
    for (const auto &m : modules)
      if (m.bus_sub_port == port)
        return m.name;
    return nullptr;
  }

  size_t num_bus() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_bus(); });
  }

  size_t num_interconnects() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_interconnect(); });
  }

  size_t num_managers() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_manager(); });
  }

  size_t num_subordinates() const {
    return std::count_if(modules.begin(), modules.end(),
                         [](const auto &m) { return m.is_subordinate(); });
  }
};