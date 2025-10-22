#pragma once

#include <yaml-cpp/yaml.h>

#include "logging.h"

#include "modules/AXIBus.h"
#include "modules/Cache.h"
#include "modules/Core.h"
#include "modules/DMAEngine.h"
#include "modules/Memory.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/Manager.h"
#include "setup/Types.h"

struct CoreChiplet : ChipletBase {
  const YAML::Node chiplet_config;
  const unsigned num_cores;

  AXIBus axi_bus;
  DMAEngine dma_engine;
  Memory memory;
  std::vector<std::unique_ptr<Core>> cores;
  std::vector<std::unique_ptr<Cache>> caches;

  CoreChiplet(sc_module_name name, unsigned id, SystemConfig sysconf,
              CoreCodeMap codemap)
      : ChipletBase(name, id, sysconf),
        chiplet_config(sysconf.chiplets[chiplet_name].config),
        num_cores(chiplet_config["cores"]["num"].as<int>()),
        axi_bus("axi_bus", chiplet_id, num_cores + 2, 2, chiplet_config),
        dma_engine("dma_engine", chiplet_config),
        memory("memory", chiplet_config) {
    // Assertions
    LOG_ASSERT(chiplet_config["axi"]["width"].as<int>() >= 8 ||
                   (chiplet_config["axi"]["width"].as<int>() % 8) == 0,
               "Parameter Error: AXI size must be a multiple of 8");

    // Cores/Caches
    for (int i = 0; i < num_cores; ++i) {
      std::string core_name = "core" + std::to_string(i);
      std::string cache_name = "cache" + std::to_string(i);

      cores.push_back(std::make_unique<Core>(core_name.c_str(), chiplet_id, i,
                                             chiplet_config));
      caches.push_back(std::make_unique<Cache>(cache_name.c_str(), chiplet_id,
                                               chiplet_config));

      // Bind clocks and sockets
      cores[i]->clk.bind(system_clk);
      cores[i]->isocket.bind(caches[i]->tsocket);
      caches[i]->clk.bind(system_clk);
      caches[i]->isocket.bind(*axi_bus.mgr_tsockets[i]);

      // Assign program code
      auto it = codemap.find({chiplet_name, i});
      if (it != codemap.end()) {
        cores[i]->thread_fn = it->second.first;
        cores[i]->interrupt_fn = it->second.second;
      } else {
        LOG_WARN(chiplet_name << "." << core_name
                              << " has no code assigned. Ignoring.");
      }
    }

    // Memory
    memory.clk.bind(system_clk);
    memory.tsocket.bind(*axi_bus.sub_isockets[0]);

    // DMA Engine
    dma_engine.clk.bind(system_clk);
    dma_engine.isocket.bind(*axi_bus.mgr_tsockets[num_cores]);
    DMAEngine *dma_engine_ptr =
        (sysconf.interconnect.config["use_dma"] &&
         !sysconf.interconnect.config["use_dma"].as<bool>())
            ? nullptr
            : &dma_engine;

    // Interconnect
    InterconnectManager manager(chiplet_id, sysconf.chiplets[chiplet_name],
                                sysconf.interconnect, num_cores,
                                dma_engine_ptr);
    interconnect = manager.create_interconnect();
    interconnect->bind_clock(system_clk);
    interconnect->axi_in_port->bind(*axi_bus.sub_isockets[1]);
    interconnect->axi_out_port->bind(*axi_bus.mgr_tsockets[num_cores + 1]);
    for (int i = 0; i < num_cores; ++i)
      interconnect->irq_ports[i]->bind(cores[i]->irq_socket);
  }
};