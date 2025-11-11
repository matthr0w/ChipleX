#pragma once

#include <yaml-cpp/yaml.h>

#include "logging.h"

#include "modules/Bus.h"
#include "modules/Cache.h"
#include "modules/Core.h"
#include "modules/DMAEngine.h"
#include "modules/Memory.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/Manager.h"
#include "setup/Types.h"

struct CoreChiplet : ChipletBase {
  // ChipletDescriptor
  // ID | Module
  // -- | --------------
  //  0 | memory
  //  1 | dma_engine
  //  2 | interconnect
  //  3 | bus
  // 4- | cores & caches

  const YAML::Node chiplet_config;
  const unsigned num_cores;

  std::vector<std::unique_ptr<Core>> cores;
  std::vector<std::unique_ptr<Cache>> caches;

  Bus bus;
  Memory memory;
  DMAEngine dma_engine;

  static ChipletDescriptor build_descriptor(unsigned chiplet_id,
                                            std::string chiplet_name,
                                            SystemConfig sysconf) {
    ChipletDescriptor desc;
    desc.chiplet_id = chiplet_id;
    desc.chiplet_name = chiplet_name;

    desc.modules.push_back({0, "memory", {AXIModuleType::SUBORDINATE}});
    desc.modules.push_back(
        {1,
         "dma_engine",
         {AXIModuleType::MANAGER, AXIModuleType::SUBORDINATE}});
    desc.modules.push_back(
        {2,
         "interconnect",
         {AXIModuleType::INTERCONNECT, AXIModuleType::MANAGER,
          AXIModuleType::SUBORDINATE}});
    desc.modules.push_back({3, "bus", {AXIModuleType::BUS}});

    // Cores/Caches
    unsigned num_cores =
        sysconf.chiplets[chiplet_name].config["cores"]["num"].as<unsigned>();
    for (unsigned i = 0; i < num_cores; ++i) {
      desc.modules.push_back(
          {4 + i, "core" + std::to_string(i), {AXIModuleType::NONE}});
      desc.modules.push_back({4 + i + num_cores,
                              "cache" + std::to_string(i),
                              {AXIModuleType::MANAGER}});
    }

    return desc;
  }

  CoreChiplet(sc_module_name name, unsigned id, SystemConfig sysconf)
      : ChipletBase(name, build_descriptor(id, std::string(name), sysconf),
                    sysconf),
        chiplet_config(sysconf.chiplets[chiplet_name].config),
        num_cores(chiplet_config["cores"]["num"].as<unsigned>()),
        memory(chiplet_desc.get(0)->name.c_str(), chiplet_config),
        dma_engine(chiplet_desc.get(1)->name.c_str(), chiplet_config),
        bus(chiplet_desc.get(3)->name.c_str(), chiplet_id,
            chiplet_desc.num_managers(), chiplet_desc.num_subordinates(),
            chiplet_config) {
    // Assertions
    LOG_ASSERT(chiplet_config["axi"]["width"].as<unsigned>() >= 8 ||
                   (chiplet_config["axi"]["width"].as<unsigned>() % 8) == 0,
               "Parameter Error: AXI size must be a multiple of 8");

    // Cores/Caches
    for (unsigned i = 0; i < num_cores; ++i) {
      std::string core_name = chiplet_desc.get(4 + i)->name;
      std::string cache_name = chiplet_desc.get(4 + i + num_cores)->name;

      cores.push_back(std::make_unique<Core>(
          core_name.c_str(), chiplet_id, i, chiplet_config, sysconf.cycles_db));
      caches.push_back(std::make_unique<Cache>(cache_name.c_str(), chiplet_id,
                                               chiplet_config));

      // Bind clocks and sockets
      cores[i]->clk.bind(chiplet_clocks.get("cores"));
      cores[i]->isocket.bind(caches[i]->tsocket);
      caches[i]->clk.bind(chiplet_clocks.get("caches"));
      caches[i]->isocket.bind(*bus.mgr_tsockets[i]);

      // Assign program code
      auto it = sysconf.program_code.find({chiplet_name, i});
      if (it != sysconf.program_code.end()) {
        cores[i]->thread_fn = it->second.first;
        cores[i]->interrupt_fn = it->second.second;
      } else {
        LOG_WARN(chiplet_name << "." << core_name
                              << " has no code assigned. Ignoring.");
      }
    }

    // Bus
    bus.clk.bind(chiplet_clocks.get("axi"));

    // Memory
    memory.clk.bind(chiplet_clocks.get("memory"));
    memory.tsocket.bind(*bus.sub_isockets[0]);

    // DMA Engine
    dma_engine.clk.bind(chiplet_clocks.get("dma_engine"));
    dma_engine.tsocket.bind(*bus.sub_isockets[1]);
    dma_engine.isocket.bind(*bus.mgr_tsockets[num_cores]);
    DMAEngine *dma_engine_ptr =
        (sysconf.interconnect.config["use_dma"] &&
         !sysconf.interconnect.config["use_dma"].as<bool>())
            ? nullptr
            : &dma_engine;

    // Interconnect
    InterconnectManager manager(chiplet_id, sysconf.chiplets[chiplet_name],
                                sysconf.interconnect, dma_engine_ptr);
    interconnect = manager.create_interconnect();
    interconnect->bind_clocks(interconnect_clocks);
    interconnect->axi_in_port->bind(*bus.sub_isockets[2]);
    interconnect->axi_out_port->bind(*bus.mgr_tsockets[num_cores + 1]);

    // Interrupt lines
    for (int i = 0; i < num_cores; ++i) {
      dma_engine.irq_sockets[i].bind(cores[i]->irq_sockets[0]);
      interconnect->irq_ports[i]->bind(cores[i]->irq_sockets[1]);
    }
  }
};