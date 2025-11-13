#pragma once

#include <yaml-cpp/yaml.h>

#include "logging.h"

#include "modules/Bus.h"
#include "modules/Cache.h"
#include "modules/Core.h"
#include "modules/DMAEngine.h"
#include "modules/Memory.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/chiplets/ChipletDescriptor.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/Manager.h"
#include "setup/Types.h"

struct CoreChiplet : ChipletBase {
  // ChipletDescriptor
  // ID | Module
  // -- | --------------
  //  0 | Memory
  //  1 | DMA Engine
  // 2- | Interconnects
  //  - | Bus
  //  - | Cores/Caches

  static ChipletDescriptor build_descriptor(std::string name, unsigned id,
                                            SystemConfig sysconf) {
    ChipletConfig chiplet_config = sysconf.chiplets[name];
    ChipletDescriptor desc;
    desc.chiplet_id = id;
    desc.chiplet_name = name;

    unsigned module_id = 0;

    // Memory
    desc.modules.push_back(
        {module_id++, MEMORY_MODULE_NAME, {AXIModuleType::SUBORDINATE}});

    // DMA Engine
    desc.modules.push_back(
        {module_id++,
         DMA_ENGINE_MODULE_NAME,
         {AXIModuleType::MANAGER, AXIModuleType::SUBORDINATE}});

    // Interconnects
    for (const auto &[name, config] : chiplet_config.interconnects)
      desc.modules.push_back(
          {module_id++,
           name,
           {AXIModuleType::INTERCONNECT, AXIModuleType::MANAGER,
            AXIModuleType::SUBORDINATE}});

    // Bus
    desc.modules.push_back(
        {module_id++, BUS_MODULE_NAME, {AXIModuleType::BUS}});

    // Cores/Caches
    unsigned num_cores = chiplet_config.node["cores"]["num"].as<unsigned>();
    for (unsigned i = 0; i < num_cores; ++i) {
      desc.modules.push_back({module_id + i,
                              CORE_MODULE_NAME + std::to_string(i),
                              {AXIModuleType::NONE}});
      desc.modules.push_back({module_id + i + num_cores,
                              CACHE_MODULE_NAME + std::to_string(i),
                              {AXIModuleType::MANAGER}});
    }

    return desc;
  }

  // Memory
  Memory memory;

  // DMA Engine
  DMAEngine dma_engine;

  // Bus
  Bus bus;

  // Cores/Caches
  const unsigned num_cores;
  std::vector<std::unique_ptr<Core>> cores;
  std::vector<std::unique_ptr<Cache>> caches;

  CoreChiplet(sc_module_name name, unsigned id, SystemConfig sysconf)
      : ChipletBase(name, id, sysconf,
                    build_descriptor(std::string(name), id, sysconf)),
        num_cores(chiplet_config.node["cores"]["num"].as<unsigned>()),
        memory(chiplet_desc.get(0)->name.c_str(), chiplet_config.node),
        dma_engine(chiplet_desc.get(1)->name.c_str(), chiplet_config.node),
        bus(chiplet_desc.get(chiplet_desc.num_interconnects() + 2)
                ->name.c_str(),
            chiplet_id, chiplet_desc.num_managers(),
            chiplet_desc.num_subordinates(), chiplet_config.node) {
    // Assertions
    LOG_ASSERT(chiplet_config.node["axi"]["width"].as<unsigned>() >= 8 ||
                   (chiplet_config.node["axi"]["width"].as<unsigned>() % 8) ==
                       0,
               "Parameter Error: AXI size must be a multiple of 8");

    // Cores/Caches
    unsigned module_idx = chiplet_desc.num_interconnects() + 3;
    unsigned num_irqs = chiplet_desc.num_interconnects() + 1; // + DMA Engine
    for (unsigned i = 0; i < num_cores; ++i) {
      std::string core_name = chiplet_desc.get(module_idx + i)->name;
      std::string cache_name =
          chiplet_desc.get(module_idx + i + num_cores)->name;

      cores.push_back(std::make_unique<Core>(core_name.c_str(), chiplet_id, i,
                                             chiplet_config.node,
                                             sysconf.cycles, num_irqs));
      caches.push_back(std::make_unique<Cache>(cache_name.c_str(), chiplet_id,
                                               chiplet_config.node));

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

    // Memory
    memory.clk.bind(chiplet_clocks.get("memory"));
    memory.tsocket.bind(*bus.sub_isockets[0]);

    // DMA Engine
    dma_engine.clk.bind(chiplet_clocks.get("dma_engine"));
    dma_engine.tsocket.bind(*bus.sub_isockets[1]);
    dma_engine.isocket.bind(*bus.mgr_tsockets[num_cores]);

    // Bus
    bus.clk.bind(chiplet_clocks.get("axi"));

    // Interconnects
    unsigned bus_sub_idx = 2;
    unsigned bus_mgr_idx = num_cores + 1;
    InterconnectManager manager(chiplet_id, chiplet_config);
    for (const auto &[name, config] : chiplet_config.interconnects) {
      const unsigned id = chiplet_config.interconnect_ids.find(name)->second;
      DMAEngine *dma_engine_ptr =
          (config.node["use_dma"] && !config.node["use_dma"].as<bool>())
              ? nullptr
              : &dma_engine;
      auto interconnect =
          manager.create_interconnect(name, id, config, dma_engine_ptr);
      interconnect->bind_clocks(get_interconnect_clocks(name));
      interconnect->axi_in_port->bind(*bus.sub_isockets[bus_sub_idx++]);
      interconnect->axi_out_port->bind(*bus.mgr_tsockets[bus_mgr_idx++]);
      interconnects.emplace(name, std::move(interconnect));
    }

    // Interrupt lines
    // DMA Engine
    for (unsigned i = 0; i < num_cores; ++i)
      dma_engine.irq_sockets[i].bind(cores[i]->irq_sockets[0]);
    // Interconnects
    unsigned irq_idx = 1;
    for (const auto &[name, config] : chiplet_config.interconnects) {
      for (unsigned i = 0; i < num_cores; ++i)
        interconnects[name]->irq_ports[i]->bind(cores[i]->irq_sockets[irq_idx]);
      irq_idx++;
    }
  }
};