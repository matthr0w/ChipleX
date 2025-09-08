#include "modules/Chiplet.h"
#include "modules/interconnects/utils.h"

#include "globals.h"

unsigned int Chiplet::instance = 1; // id == 0 is reserved for FPGA

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      axi_clk("AXI_Clk", axi_clk_cycle, sc_core::SC_NS, 0.5),
      axi_bus("AXI_Bus", chiplet_id, axi_width, num_axi_managers,
              num_axi_subordinates),
      memory("Memory", axi_width, ram_size),
      dma_engine("DMAEngine", axi_width) {
  // Cores/Caches
  for (unsigned i = 0; i < num_cores; ++i) {
    std::string core_name = "Core" + std::to_string(i);
    cores.push_back(std::make_unique<Core>(core_name.c_str(), chiplet_id, i,
                                           axi_width, cores_irq_delay));

    std::string cache_name = "Cache" + std::to_string(i);
    caches.push_back(std::make_unique<Cache>(
        cache_name.c_str(), chiplet_id, axi_width, cache_size, cache_block_size,
        cache_store_buffer_size));
  }

  for (unsigned i = 0; i < num_cores; ++i) {
    cores[i]->clock.bind(axi_clk);
    cores[i]->isocket.bind(caches[i]->tsocket);
    caches[i]->clock.bind(axi_clk);
    caches[i]->isocket.bind(*axi_bus.mgr_tsockets[i]);
  }

  // Memory
  memory.clock.bind(axi_clk);

  // AXI Bus
  axi_bus.sub_isockets[0]->bind(memory.tsocket);

  // DMA Engine
  dma_engine.clock.bind(axi_clk);
  dma_engine.isocket.bind(*axi_bus.mgr_tsockets[num_axi_managers - 1]);

  // Interconnect
  interconnect =
      create_interconnect(to_string(connection_type), chiplet_id, axi_width,
                          num_cores, num_interconnects, dma_engine);

  interconnect->bind_axi(axi_bus, axi_clk);
  for (unsigned i = 0; i < num_cores; ++i) {
    interconnect->bind_core(i, *cores[i]);
  }

  // Trackers
  // utilization
  // utilization_trackers.push_back(&core0.utilization_tracker);
  // utilization_trackers.push_back(&core1.utilization_tracker);
  // utilization_trackers.push_back(&cache0.utilization_tracker);
  // utilization_trackers.push_back(&cache1.utilization_tracker);
  // utilization_trackers.push_back(&memory_controller.utilization_tracker);
  // utilization_trackers.push_back(&ram.utilization_tracker);
  // utilization_trackers.push_back(&interconnect_protocol.utilization_tracker);

  // for (unsigned int i = 0; i < num_interconnects; ++i) {
  //   utilization_trackers.push_back(&interconnects[i]->utilization_tracker);
  // }

  // // buffer usages
  // for (unsigned int i = 0; i < num_interconnects; ++i) {
  //   buffer_trackers.push_back(&interconnects[i]->tx_tracker);
  //   buffer_trackers.push_back(&interconnects[i]->rx_tracker);
  // }
}