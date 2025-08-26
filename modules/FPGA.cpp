#include "FPGA.h"

#include "include/globals.h"

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
      // interconnect_protocol("InterconnectProtocol", fpga_id, num_cores,
      //                       num_interconnects, interconnect_pre_delay,
      //                       interconnect_post_delay),
      memory("Memory", ram_size) {
  // for (unsigned int i = 0; i < num_interconnects; ++i) {
  //   std::string name = "Interconnect" + std::to_string(i);
  //   interconnects.push_back(new Interconnect(
  //       name.c_str(), interconnect_buffer_size, interconnect_flit_size,
  //       interconnect_bandwidth_fpga, fpga_distance_mm));
  // }

  initialize();
}

FPGA::~FPGA() {
  // for (auto *interconnect : interconnects) {
  //   delete interconnect;
  // }
  // interconnects.clear();
}

void FPGA::initialize() {
  // -------------------------------------------------------
  // clocks
  // -------------------------------------------------------
  core.clock.bind(axi_clk);
  memory.clock.bind(axi_clk);

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  // AXI
  axi_bus.sub_isockets[0]->bind(memory.tsocket);

  // cores
  core.isocket.bind(*axi_bus.mgr_tsockets[0]);

  // caches
  // cache.isocket.bind(axi_manager_core.tsocket);

  // interconnects
  // interconnect_protocol.axi_isocket.bind(axi_manager_interconnect.tsocket);

  // // IRQs
  // interconnect_protocol.irq_sockets[0].bind(core.irq_socket);

  // interconnect protocol <-> interconnects
  // for (unsigned int i = 0; i < num_interconnects; ++i) {
  //   interconnect_protocol.phy_isockets[i].bind(
  //       interconnects[i]->protocol_tsocket);

  //   interconnects[i]->protocol_isocket.bind(
  //       interconnect_protocol.phy_tsockets[i]);
  // }

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