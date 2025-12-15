#pragma once

#include "logging.h"

#include "modules/Memory.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/chiplets/ChipletDescriptor.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/Manager.h"
#include "setup/Types.h"

struct MemoryChiplet : ChipletBase {
  static ChipletDescriptor build_descriptor(std::string name, unsigned id,
                                            ChipletConfig chiplet_config) {
    ChipletDescriptor desc;
    desc.chiplet_id = id;
    desc.chiplet_name = name;

    unsigned module_id = 0;

    // Memory
    desc.add_module(MEMORY_MODULE_NAME, {AXIModuleType::SUBORDINATE});

    // Interconnect
    const auto &first_it = *chiplet_config.interconnects.begin();
    const std::string &interconnect_name = first_it.first;
    const InterconnectConfig &interconnect_config = first_it.second;

    if (chiplet_config.interconnects.size() > 1)
      LOG_WARN(
          "Chiplet " << name << " of type " << chiplet_config.type.to_string()
                     << " has multiple interconnects defined. Interconnect "
                     << interconnect_name << " will be used.");

    desc.add_module(interconnect_name,
                    {AXIModuleType::INTERCONNECT, AXIModuleType::MANAGER,
                     AXIModuleType::SUBORDINATE});

    return desc;
  }

  // Memory
  Memory memory;

  // Dummy AXI port for interconnect
  ARM::AXI::SimpleInitiatorSocket<MemoryChiplet> dummy_axi_port;

  MemoryChiplet(sc_module_name name, unsigned id, ChipletConfig chiplet_config)
      : ChipletBase(name, id, chiplet_config,
                    build_descriptor(std::string(name), id, chiplet_config)),
        memory(MEMORY_MODULE_NAME.c_str(), chiplet_config),
        dummy_axi_port("dummy_axi_port", *this, nullptr,
                       ARM::TLM::PROTOCOL_AXI4,
                       chiplet_config.node["axi"]["width"].as<unsigned>()) {
    // Assertions
    LOG_ASSERT(chiplet_config.node["axi"]["width"].as<unsigned>() >= 8 ||
                   (chiplet_config.node["axi"]["width"].as<unsigned>() % 8) ==
                       0,
               "Parameter Error: AXI size must be a multiple of 8");

    // Memory
    memory.clk.bind(chiplet_clocks.get(MEMORY_MODULE_NAME));

    // Interconnect
    InterconnectManager manager(chiplet_id, chiplet_config);
    for (const auto &[name, config] : chiplet_config.interconnects) {
      const unsigned id = chiplet_config.interconnect_ids.find(name)->second;
      auto interconnect =
          manager.create_interconnect(name, id, config, nullptr);
      interconnect->bind_clocks(get_interconnect_clocks(name));
      interconnect->axi_in_port->bind(dummy_axi_port);
      interconnect->axi_out_port->bind(memory.tsocket);
      interconnects.emplace(name, std::move(interconnect));
    }
  }
};