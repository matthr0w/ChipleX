#include "modules/Chiplet.h"

#include "globals.h"

unsigned int Chiplet::instance = 1; // id == 0 is reserved for FPGA

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      axi_clk("AXI_Clk", axi_clk_cycle, sc_core::SC_NS, 0.5),
      axi_bus("AXI_Bus", chiplet_id, axi_width, num_axi_managers,
              num_axi_subordinates),
      core0("Core0", chiplet_id, 0, axi_width, interconnect_irq_delay),
      core1("Core1", chiplet_id, 1, axi_width, interconnect_irq_delay),
      cache0("Cache0", chiplet_id, axi_width, cache_size, cache_block_size,
             cache_store_buffer_size),
      cache1("Cache1", chiplet_id, axi_width, cache_size, cache_block_size,
             cache_store_buffer_size),
      memory("Memory", axi_width, ram_size), dma_engine("DMAEngine", axi_width),
      interconnect(
          "Interconnect", chiplet_id, axi_width, num_cores, num_interconnects,
          interconnect_flit_size, interconnect_overhead_size,
          interconnect_staging_buffer_size, interconnect_link_buffer_size,
          interconnect_bandwidth_chiplets, chiplet_distance_um, &dma_engine) {
  initialize();
}

void Chiplet::initialize() {
  // -------------------------------------------------------
  // clocks
  // -------------------------------------------------------
  core0.clock.bind(axi_clk);
  core1.clock.bind(axi_clk);
  cache0.clock.bind(axi_clk);
  cache1.clock.bind(axi_clk);
  memory.clock.bind(axi_clk);
  dma_engine.clock.bind(axi_clk);
  interconnect.axi_clock.bind(axi_clk);
  interconnect.protocol_clock.bind(axi_clk);
  interconnect.phy_clock.bind(axi_clk);

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  // AXI
  axi_bus.sub_isockets[0]->bind(memory.tsocket);
  axi_bus.sub_isockets[1]->bind(interconnect.axi_tsocket);

  // cores
  core0.isocket.bind(cache0.tsocket);
  core1.isocket.bind(cache1.tsocket);

  // caches
  cache0.isocket.bind(*axi_bus.mgr_tsockets[0]);
  cache1.isocket.bind(*axi_bus.mgr_tsockets[1]);

  // dma engine
  dma_engine.isocket.bind(*axi_bus.mgr_tsockets[2]);

  // interconnect
  interconnect.irq_sockets[0].bind(core0.irq_socket);
  interconnect.irq_sockets[1].bind(core1.irq_socket);

  // trackers
  // utilization
  utilization_trackers.push_back(&core0.utilization_tracker);
  utilization_trackers.push_back(&core1.utilization_tracker);
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