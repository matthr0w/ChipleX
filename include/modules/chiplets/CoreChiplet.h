#include "modules/AXIBus.h"
#include "modules/Cache.h"
#include "modules/Core.h"
#include "modules/DMAEngine.h"
#include "modules/Memory.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/Manager.h"

#include "usercode/UserCode.h"

struct CoreChiplet : ChipletBase {
  const unsigned num_cores;

  AXIBus axi_bus;
  DMAEngine dma_engine;
  Memory memory;
  std::vector<std::unique_ptr<Core>> cores;
  std::vector<std::unique_ptr<Cache>> caches;

  CoreChiplet(sc_core::sc_module_name name, unsigned id, SystemConfig sysconf)
      : ChipletBase(name, id, sysconf),
        num_cores(sysconf.chiplets[std::string(name)]
                      .config["cores"]["num"]
                      .as<int>()),
        axi_bus("axi_bus", chiplet_id, num_cores + 2, 2,
                sysconf.chiplets[chiplet_name].config),
        dma_engine("dma_engine", sysconf.chiplets[chiplet_name].config),
        memory("memory", sysconf.chiplets[chiplet_name].config) {
    // Cores/Caches
    for (int i = 0; i < num_cores; ++i) {
      cores.push_back(std::make_unique<Core>(
          std::string("core" + std::to_string(i)).c_str(), chiplet_id, i,
          sysconf.chiplets[chiplet_name].config));
      caches.push_back(std::make_unique<Cache>(
          std::string("cache" + std::to_string(i)).c_str(), chiplet_id,
          sysconf.chiplets[chiplet_name].config));

      // Bind clocks and sockets
      cores[i]->clk.bind(system_clk);
      cores[i]->isocket.bind(caches[i]->tsocket);
      caches[i]->clk.bind(system_clk);
      caches[i]->isocket.bind(*axi_bus.mgr_tsockets[i]);

      // Assign user code
      auto it = core_code.find({chiplet_id, i});
      if (it != core_code.end()) {
        cores[i]->thread_fn = it->second.first;
        cores[i]->interrupt_fn = it->second.second;
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
                                sysconf.interconnect, dma_engine_ptr);
    interconnect = manager.create_interconnect();
    interconnect->bind_clock(system_clk);
    interconnect->axi_in_port->bind(*axi_bus.sub_isockets[1]);
    interconnect->axi_out_port->bind(*axi_bus.mgr_tsockets[num_cores + 1]);
    for (int i = 0; i < num_cores; ++i)
      interconnect->irq_ports[i]->bind(cores[i]->irq_socket);
  }
};