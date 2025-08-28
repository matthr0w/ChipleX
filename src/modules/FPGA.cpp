#include "modules/FPGA.h"

#include "globals.h"

using namespace sc_core;
using namespace tlm;

FPGA::FPGA(sc_module_name name)
    : sc_module(name), fpga_id(0),
      axi_clk("AXI_Clk", axi_clk_cycle, sc_core::SC_NS, 0.5),
      axi_bus("AXI_Bus", fpga_id, axi_width, num_axi_managers,
              num_axi_subordinates),
      core("Core", fpga_id, 0, axi_width, interconnect_irq_delay),
      // cache("Cache", axi_utils, fpga_id, cache_size, cache_block_size,
      //       cache_store_buffer_size, cache_arbitration_delay,
      //       cache_access_delay),
      interconnect("Interconnect", fpga_id, num_cores, num_interconnects,
                   interconnect_buffer_size, interconnect_flit_size,
                   interconnect_overhead_size, interconnect_bandwidth_chiplets,
                   chiplet_distance_um, axi_width),
      memory("Memory", ram_size, axi_width) {
  initialize();
}

void FPGA::initialize() {
  // -------------------------------------------------------
  // clocks
  // -------------------------------------------------------
  core.clock.bind(axi_clk);
  memory.clock.bind(axi_clk);
  interconnect.clock.bind(axi_clk);

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  // AXI
  axi_bus.sub_isockets[0]->bind(memory.tsocket);
  axi_bus.sub_isockets[1]->bind(interconnect.axi_tsocket);

  // cores
  core.isocket.bind(*axi_bus.mgr_tsockets[0]);

  // caches
  // cache.isocket.bind(axi_manager_core.tsocket);

  // interconnect
  // interconnect_protocol.axi_isocket.bind(axi_manager_interconnect.tsocket);
  interconnect.irq_sockets[0].bind(core.irq_socket);

  // trackers
  // utilization
  utilization_trackers.push_back(&core.utilization_tracker);
  // utilization_trackers.push_back(&cache.utilization_tracker);
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