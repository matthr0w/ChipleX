#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "logging.h"

#include "modules/chiplets/ChipletDescriptor.h"

class ChipletRegistry {
  private:
    std::unordered_map<std::string, std::shared_ptr<ChipletDescriptor>> chiplets_by_name_;
    std::unordered_map<unsigned, std::shared_ptr<ChipletDescriptor>>    chiplets_by_id_;

    ChipletRegistry() = default;

  public:
    static ChipletRegistry &instance() {
        static ChipletRegistry instance_;
        return instance_;
    }

    ChipletRegistry(const ChipletRegistry &)            = delete;
    ChipletRegistry &operator=(const ChipletRegistry &) = delete;

    // Register chiplet by ID and name
    void register_chiplet(unsigned id, const std::string &name, const ChipletDescriptor &desc) {
        auto ptr                = std::make_shared<ChipletDescriptor>(desc);
        chiplets_by_name_[name] = ptr;
        chiplets_by_id_[id]     = ptr;
    }

    // Get chiplet by ID
    std::shared_ptr<const ChipletDescriptor> get(unsigned id) const {
        auto it = chiplets_by_id_.find(id);
        if (it == chiplets_by_id_.end()) {
            LOG_ERROR("Chiplet not found: ID" << id);
        }
        return it->second;
    }

    // Get chiplet by name
    std::shared_ptr<const ChipletDescriptor> get(const std::string &name) const {
        auto it = chiplets_by_name_.find(name);
        if (it == chiplets_by_name_.end()) {
            LOG_ERROR("Chiplet not found: " << name);
        }
        return it->second;
    }

    // Global lookup: (chiplet_id, module_id) -> chiplet_module
    const ChipletModuleInfo *get_module(const unsigned &chiplet_id, const unsigned &module_id) const {
        auto desc = get(chiplet_id);
        return desc->get(module_id);
    }

    // Global lookup: (chiplet_id, module_name) -> chiplet_module
    const ChipletModuleInfo *get_module(const unsigned &chiplet_id, const std::string &module_name) const {
        auto desc = get(chiplet_id);
        return desc->get(module_name);
    }

    // Global lookup: (chiplet_name, module_id) -> chiplet_module
    const ChipletModuleInfo *get_module(const std::string &chiplet_name, const unsigned &module_id) const {
        auto desc = get(chiplet_name);
        return desc->get(module_id);
    }

    // Global lookup: (chiplet_name, module_name) -> chiplet_module
    const ChipletModuleInfo *get_module(const std::string &chiplet_name, const std::string &module_name) const {
        auto desc = get(chiplet_name);
        return desc->get(module_name);
    }

    // Global lookup: (chiplet_id, mgr_port) -> module_name
    const std::string get_module_name_at_mgr_port(const unsigned &chiplet_id, const unsigned &port) const {
        auto desc = get(chiplet_id);
        return desc->get_name_at_mgr_port(port);
    }

    // Global lookup: (chiplet_id, sub_port) -> module_name
    const std::string get_module_name_at_sub_port(const unsigned &chiplet_id, const unsigned &port) const {
        auto desc = get(chiplet_id);
        return desc->get_name_at_sub_port(port);
    }
};