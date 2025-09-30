#include "modules/Chiplet.h"
#include "modules/interconnects/Manager.h"

#include "common/System.h"

unsigned Chiplet::instance = 0;

Chiplet::Chiplet(sc_module_name name, SystemConfig sysconf)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      chiplet_config(sysconf.chiplets[std::string(name)]),
      interconnect_config(sysconf.interconnect),
      num_cores(chiplet_config.config["cores"]["num"].as<int>()),
      num_managers(init_num_managers()),
      num_subordinates(init_num_subordinates()),
      system_clk("system_clock",
                 chiplet_config.config["axi"]["clk_cycle"].as<int>(),
                 sc_core::SC_NS, 0.5),
      axi_bus("axi_bus", chiplet_id, num_managers, num_subordinates,
              chiplet_config.config),
      dma_engine("dma_engine", chiplet_config.config),
      memory("memory", chiplet_config.config) {
  switch (chiplet_config.type.type) {
  case ChipletType::Type::SingleCore:
  case ChipletType::Type::DualCore:
  case ChipletType::Type::QuadCore: {
    // Cores/Caches
    for (int i = 0; i < num_cores; ++i) {
      std::string core_name = "core" + std::to_string(i);
      cores.push_back(std::make_unique<Core>(core_name.c_str(), chiplet_id, i,
                                             chiplet_config.config));
      std::string cache_name = "cache" + std::to_string(i);
      caches.push_back(std::make_unique<Cache>(cache_name.c_str(), chiplet_id,
                                               chiplet_config.config));
    }

    for (int i = 0; i < num_cores; ++i) {
      cores[i]->clk.bind(system_clk);
      cores[i]->isocket.bind(caches[i]->tsocket);
      caches[i]->clk.bind(system_clk);
      caches[i]->isocket.bind(*axi_bus.mgr_tsockets[i]);
    }

    // Memory
    memory.clk.bind(system_clk);

    // AXI Bus
    axi_bus.sub_isockets[0]->bind(memory.tsocket);

    // DMA Engine
    dma_engine.clk.bind(system_clk);
    dma_engine.isocket.bind(*axi_bus.mgr_tsockets[num_cores]);

    // Interconnect
    InterconnectManager manager(chiplet_id, chiplet_config, interconnect_config,
                                &dma_engine);
    interconnect = manager.create_interconnect();
    interconnect->bind_clock(system_clk);
    interconnect->axi_in_port->bind(*axi_bus.sub_isockets[1]);
    interconnect->axi_out_port->bind(*axi_bus.mgr_tsockets[num_cores + 1]);
    for (unsigned i = 0; i < num_cores; ++i)
      interconnect->irq_ports[i]->bind(cores[i]->irq_socket);

    break;
  }

  case ChipletType::Type::Memory: {
    // TODO: Implement memory chiplet

    break;
  }

  default:
    break;
  }
}

unsigned Chiplet::init_num_managers() {
  switch (chiplet_config.type.type) {
  case ChipletType::Type::SingleCore:
  case ChipletType::Type::DualCore:
  case ChipletType::Type::QuadCore:
    // Cores + DMA Engine + Interconnect
    return num_cores + 2;
  case ChipletType::Type::Memory:
    // DMA Engine + Interconnect
    return 2;
  default:
    return 0;
  }
}

unsigned Chiplet::init_num_subordinates() {
  switch (chiplet_config.type.type) {
  case ChipletType::Type::SingleCore:
  case ChipletType::Type::DualCore:
  case ChipletType::Type::QuadCore:
    // Memory + Interconnect
    return 2;
  case ChipletType::Type::Memory:
    // Memory
    return 1;
  default:
    return 0;
  }
}