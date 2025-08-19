#include "Chiplet.h"

#include "include/globals.h"

unsigned int Chiplet::instance = 1; // id == 0 is reserved for FPGA

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      core0("Core0", chiplet_id, 0, interconnect_irq_delay),
      core1("Core1", chiplet_id, 1, interconnect_irq_delay),
      cache0("Cache0", chiplet_id, cache_size, cache_block_size,
             cache_arbitration_delay, cache_access_delay),
      cache1("Cache1", chiplet_id, cache_size, cache_block_size,
             cache_arbitration_delay, cache_access_delay),
      bus("Bus", chiplet_id, num_axi_managers, num_axi_subordinates,
          bus_arbitration_delay),
      axi_interconnect("AXI_Interconnect", chiplet_id, 2, 1, axi_clk_cycle,
                       axi_arbitration_delay),
      axi_manager_core0("AXI_M_Core0", axi_width, axi_clk_cycle),
      axi_manager_core1("AXI_M_Core1", axi_width, axi_clk_cycle),
      axi_subordinate_memory_controller("AXI_S_MemoryController",
                                        axi_width, axi_clk_cycle),
      interconnect_protocol("InterconnectProtocol", chiplet_id, num_cores,
                            num_interconnects, interconnect_flit_size,
                            interconnect_overhead_size, interconnect_pre_delay,
                            interconnect_post_delay, interconnect_irq_delay,
                            bus_width, bus_clk_cycle),
      memory_controller("MemoryController", ram_size,
                        memory_controller_address_delay),
      ram("RAM", ram_size, ram_width, ram_clk_cycle, ram_access_delay) {
  for (unsigned int i = 0; i < num_interconnects; ++i) {
    std::string name = "Interconnect" + std::to_string(i);

    if (i == 0) { // interconnect to FPGA
      interconnects.push_back(new Interconnect(
          name.c_str(), interconnect_buffer_size, interconnect_flit_size,
          interconnect_overhead_size, interconnect_pre_delay,
          interconnect_post_delay, interconnect_irq_delay,
          interconnect_bandwidth_fpga, fpga_distance_mm));
    } else {
      interconnects.push_back(new Interconnect(
          name.c_str(), interconnect_buffer_size, interconnect_flit_size,
          interconnect_overhead_size, interconnect_pre_delay,
          interconnect_post_delay, interconnect_irq_delay,
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
  // sockets
  // cores
  core0.isocket.bind(cache0.tsocket);
  core1.isocket.bind(cache1.tsocket);

  // caches
  dummy_initiator0.bind(bus.manager_target_sockets[0]);
  dummy_initiator1.bind(bus.manager_target_sockets[1]);
  cache0.isocket.bind(axi_manager_core0.tsocket);
  cache1.isocket.bind(axi_manager_core1.tsocket);

  // AXI
  axi_manager_core0.isocket.bind(axi_interconnect.tsockets[0]);
  axi_manager_core1.isocket.bind(axi_interconnect.tsockets[1]);
  axi_interconnect.isockets[0].bind(axi_subordinate_memory_controller.tsocket);
  axi_subordinate_memory_controller.isocket.bind(memory_controller.tsocket);

  // memory controller
  bus.subordinate_initiator_sockets[1].bind(dummy_target);
  memory_controller.isocket.bind(ram.tsocket);

  // interconnects
  bus.subordinate_initiator_sockets[0].bind(
      interconnect_protocol.bus_target_socket);
  interconnect_protocol.bus_initiator_socket.bind(
      bus.manager_target_sockets[2]);

  // IRQs
  interconnect_protocol.irq_initiator_sockets[0].bind(core0.irq_socket);
  interconnect_protocol.irq_initiator_sockets[1].bind(core1.irq_socket);

  // interconnect protocol <-> interconnects
  for (unsigned int i = 0; i < 3; ++i) {
    interconnect_protocol.interconnect_initiator_sockets[i].bind(
        interconnects[i]->protocol_target_socket);

    interconnects[i]->protocol_initiator_socket.bind(
        interconnect_protocol.interconnect_target_sockets[i]);
  }

  // trackers
  // utilization
  utilization_trackers.push_back(&core0.utilization_tracker);
  utilization_trackers.push_back(&core1.utilization_tracker);
  utilization_trackers.push_back(&cache0.utilization_tracker);
  utilization_trackers.push_back(&cache1.utilization_tracker);
  utilization_trackers.push_back(&bus.utilization_tracker);
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