#include "Chiplet.h"

#include "include/globals.h"

unsigned int Chiplet::instance = 1; // id == 0 is reserved for FPGA

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      axi_utils(axi_width / 8),
      axi_interconnect("AXI_Interconnect", chiplet_id, num_axi_managers, num_axi_subordinates, axi_clk_cycle,
                       axi_arbitration_delay),
      axi_manager_core0("AXI_M_Core0", axi_width, axi_clk_cycle),
      axi_manager_core1("AXI_M_Core1", axi_width, axi_clk_cycle),
      axi_manager_interconnect("AXI_M_Interconnect", axi_width, axi_clk_cycle),
      axi_subordinate_interconnect("AXI_S_Interconnect", axi_width,
                                   axi_clk_cycle),
      axi_subordinate_memory_controller("AXI_S_MemoryController", axi_width,
                                        axi_clk_cycle),
      core0("Core0", axi_utils, chiplet_id, 0, interconnect_irq_delay),
      core1("Core1", axi_utils, chiplet_id, 1, interconnect_irq_delay),
      cache0("Cache0", axi_utils, chiplet_id, cache_size, cache_block_size,
             cache_store_buffer_size, cache_arbitration_delay,
             cache_access_delay),
      cache1("Cache1", axi_utils, chiplet_id, cache_size, cache_block_size,
             cache_store_buffer_size, cache_arbitration_delay,
             cache_access_delay),
      interconnect_protocol("InterconnectProtocol", chiplet_id, num_cores,
                            num_interconnects, interconnect_pre_delay,
                            interconnect_post_delay),
      memory_controller("MemoryController", ram_size,
                        memory_controller_address_delay),
      ram("RAM", ram_size, ram_width, ram_clk_cycle, ram_access_delay) {
  for (unsigned int i = 0; i < num_interconnects; ++i) {
    std::string name = "Interconnect" + std::to_string(i);

    if (i == 0) { // interconnect to FPGA
      interconnects.push_back(new Interconnect(
          name.c_str(), interconnect_buffer_size, interconnect_flit_size,
          interconnect_bandwidth_fpga, fpga_distance_mm));
    } else {
      interconnects.push_back(new Interconnect(
          name.c_str(), interconnect_buffer_size, interconnect_flit_size,
          interconnect_bandwidth_chiplets, chiplet_distance_um / 1000));
    }
  }

  initialize();
}

Chiplet::~Chiplet() {
  for (auto *interconnect : interconnects) {
    delete interconnect;
  }
  interconnects.clear();
}

void Chiplet::initialize() {
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  // AXI
  axi_interconnect.isockets[0].bind(axi_subordinate_memory_controller.tsocket);
  axi_interconnect.isockets[1].bind(axi_subordinate_interconnect.tsocket);
  axi_manager_core0.isocket.bind(axi_interconnect.tsockets[0]);
  axi_manager_core1.isocket.bind(axi_interconnect.tsockets[1]);
  axi_manager_interconnect.isocket.bind(axi_interconnect.tsockets[2]);
  axi_subordinate_interconnect.isocket.bind(interconnect_protocol.axi_tsocket);
  axi_subordinate_memory_controller.isocket.bind(memory_controller.tsocket);

  // cores
  core0.isocket.bind(cache0.tsocket);
  core1.isocket.bind(cache1.tsocket);

  // caches
  cache0.isocket.bind(axi_manager_core0.tsocket);
  cache1.isocket.bind(axi_manager_core1.tsocket);

  // memory controller
  memory_controller.isocket.bind(ram.tsocket);

  // interconnects
  interconnect_protocol.axi_isocket.bind(axi_manager_interconnect.tsocket);

  // IRQs
  interconnect_protocol.irq_sockets[0].bind(core0.irq_socket);
  interconnect_protocol.irq_sockets[1].bind(core1.irq_socket);

  // interconnect protocol <-> interconnects
  for (unsigned int i = 0; i < 3; ++i) {
    interconnect_protocol.phy_isockets[i].bind(
        interconnects[i]->protocol_tsocket);

    interconnects[i]->protocol_isocket.bind(
        interconnect_protocol.phy_tsockets[i]);
  }

  // trackers
  // utilization
  utilization_trackers.push_back(&core0.utilization_tracker);
  utilization_trackers.push_back(&core1.utilization_tracker);
  utilization_trackers.push_back(&cache0.utilization_tracker);
  utilization_trackers.push_back(&cache1.utilization_tracker);
  utilization_trackers.push_back(&memory_controller.utilization_tracker);
  utilization_trackers.push_back(&ram.utilization_tracker);
  utilization_trackers.push_back(&interconnect_protocol.utilization_tracker);

  for (unsigned int i = 0; i < num_interconnects; ++i) {
    utilization_trackers.push_back(&interconnects[i]->utilization_tracker);
  }

  // buffer usages
  for (unsigned int i = 0; i < num_interconnects; ++i) {
    buffer_trackers.push_back(&interconnects[i]->tx_tracker);
    buffer_trackers.push_back(&interconnects[i]->rx_tracker);
  }
}