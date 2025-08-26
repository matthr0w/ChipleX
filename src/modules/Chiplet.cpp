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
      // cache0("Cache0", axi_utils, chiplet_id, cache_size, cache_block_size,
      //        cache_store_buffer_size, cache_arbitration_delay,
      //        cache_access_delay),
      // cache1("Cache1", axi_utils, chiplet_id, cache_size, cache_block_size,
      //        cache_store_buffer_size, cache_arbitration_delay,
      //        cache_access_delay),
      // interconnect_protocol("InterconnectProtocol", chiplet_id, num_cores,
      //                       num_interconnects, interconnect_pre_delay,
      //                       interconnect_post_delay),
      memory("Memory", ram_size) {
  // for (unsigned int i = 0; i < num_interconnects; ++i) {
  //   std::string name = "Interconnect" + std::to_string(i);

  //   if (i == 0) { // interconnect to FPGA
  //     interconnects.push_back(new Interconnect(
  //         name.c_str(), interconnect_buffer_size, interconnect_flit_size,
  //         interconnect_bandwidth_fpga, fpga_distance_mm));
  //   } else {
  //     interconnects.push_back(new Interconnect(
  //         name.c_str(), interconnect_buffer_size, interconnect_flit_size,
  //         interconnect_bandwidth_chiplets, chiplet_distance_um / 1000));
  //   }
  // }

  initialize();
}

Chiplet::~Chiplet() {
  // for (auto *interconnect : interconnects) {
  //   delete interconnect;
  // }
  // interconnects.clear();
}

void Chiplet::initialize() {
  // -------------------------------------------------------
  // clocks
  // -------------------------------------------------------
  core0.clock.bind(axi_clk);
  core1.clock.bind(axi_clk);
  memory.clock.bind(axi_clk);

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  // AXI
  axi_bus.sub_isockets[0]->bind(memory.tsocket);

  // cores
  core0.isocket.bind(*axi_bus.mgr_tsockets[0]);
  core1.isocket.bind(*axi_bus.mgr_tsockets[1]);

  // caches
  // cache0.isocket.bind(axi_manager_core0.tsocket);
  // cache1.isocket.bind(axi_manager_core1.tsocket);

  // interconnects
  // interconnect_protocol.axi_isocket.bind(axi_manager_interconnect.tsocket);

  // // IRQs
  // interconnect_protocol.irq_sockets[0].bind(core0.irq_socket);
  // interconnect_protocol.irq_sockets[1].bind(core1.irq_socket);

  // interconnect protocol <-> interconnects
  // for (unsigned int i = 0; i < 3; ++i) {
  //   interconnect_protocol.phy_isockets[i].bind(
  //       interconnects[i]->protocol_tsocket);

  //   interconnects[i]->protocol_isocket.bind(
  //       interconnect_protocol.phy_tsockets[i]);
  // }

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