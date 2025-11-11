#pragma once

#include "logging.h"

#include "modules/Memory.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/Manager.h"

struct MemoryChiplet : ChipletBase {
  // ChipletDescriptor
  // ID | Module
  // -- | --------------
  //  0 | memory
  //  1 | interconnect

  const YAML::Node chiplet_config;

  Memory memory;

  static ChipletDescriptor build_descriptor(unsigned chiplet_id,
                                            std::string chiplet_name) {
    ChipletDescriptor desc;
    desc.chiplet_id = chiplet_id;
    desc.chiplet_name = chiplet_name;

    desc.modules.push_back({0, "memory", {AXIModuleType::SUBORDINATE}});
    desc.modules.push_back(
        {1,
         "interconnect",
         {AXIModuleType::INTERCONNECT, AXIModuleType::MANAGER,
          AXIModuleType::SUBORDINATE}});

    return desc;
  }

  // Dummy AXI initiator socket
  ARM::AXI::SimpleInitiatorSocket<MemoryChiplet> dummy_axi_isocket;

  MemoryChiplet(sc_module_name name, unsigned id, SystemConfig sysconf)
      : ChipletBase(name, build_descriptor(id, std::string(name)), sysconf),
        chiplet_config(sysconf.chiplets[chiplet_name].config),
        memory(chiplet_desc.get(0)->name.c_str(), chiplet_config),
        dummy_axi_isocket("dummy_axi_isocket", *this, nullptr,
                          ARM::TLM::PROTOCOL_AXI4,
                          chiplet_config["axi"]["width"].as<int>()) {
    // Assertions
    LOG_ASSERT(chiplet_config["axi"]["width"].as<int>() >= 8 ||
                   (chiplet_config["axi"]["width"].as<int>() % 8) == 0,
               "Parameter Error: AXI size must be a multiple of 8");

    // Memory
    memory.clk.bind(chiplet_clocks.get("memory"));

    // Interconnect
    InterconnectManager manager(chiplet_id, sysconf.chiplets[chiplet_name],
                                sysconf.interconnect, nullptr);
    interconnect = manager.create_interconnect();
    interconnect->bind_clocks(interconnect_clocks);
    interconnect->axi_in_port->bind(dummy_axi_isocket);
    interconnect->axi_out_port->bind(memory.tsocket);
  }
};