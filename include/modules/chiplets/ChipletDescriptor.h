#pragma once

#include <algorithm>
#include <string>
#include <vector>

enum class AXIModuleType { NONE, BUS, INTERCONNECT, MANAGER, SUBORDINATE };

struct ChipletModuleInfo {
  unsigned id;
  std::string name;
  std::vector<AXIModuleType> types; // Multiple types allowed

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

  const ChipletModuleInfo *get(unsigned module_id) const {
    for (const auto &m : modules)
      if (m.id == module_id)
        return &m;
    return nullptr;
  }

  const ChipletModuleInfo *get(const std::string &name) const {
    for (const auto &m : modules)
      if (m.name == name)
        return &m;
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