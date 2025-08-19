#include "FPGA.h"

#include "include/globals.h"

using namespace sc_core;
using namespace tlm;

FPGA::FPGA(sc_module_name name)
    : sc_module(name), fpga_id(0),
      core("Core", fpga_id, 0, interconnect_irq_delay),
      cache("Cache", fpga_id, cache_size, cache_block_size,
            cache_arbitration_delay, cache_access_delay),
      bus("Bus", fpga_id, num_bus_managers, num_bus_subordinates,
          bus_arbitration_delay),
      interconnect_protocol("InterconnectProtocol", fpga_id, num_cores,
                            num_interconnects, interconnect_flit_size,
                            interconnect_overhead_size, interconnect_pre_delay,
                            interconnect_post_delay, interconnect_irq_delay,
                            bus_width, bus_clk_cycle),
      memory_controller("MemoryController", ram_size,
                        memory_controller_address_delay),
      ram("RAM", ram_size, ram_width, ram_clk_cycle, ram_access_delay) {
  for (unsigned int i = 0; i < num_interconnects; ++i) {
    std::string name = "Interconnect" + std::to_string(i);
    interconnects.push_back(new Interconnect(
        name.c_str(), interconnect_buffer_size, interconnect_flit_size,
        interconnect_overhead_size, interconnect_pre_delay,
        interconnect_post_delay, interconnect_irq_delay,
        interconnect_bandwidth_fpga, fpga_distance_mm));
  }

  initialize();
}

FPGA::~FPGA() {
  for (auto *interconnect : interconnects) {
    delete interconnect;
  }
  interconnects.clear();
}

void FPGA::initialize() {
  // sockets
  // core
  core.isocket.bind(cache.tsocket);
  // cache
  cache.isocket.bind(bus.manager_target_sockets[0]);
  // interconnects
  bus.subordinate_initiator_sockets[0].bind(
      interconnect_protocol.bus_target_socket);
  interconnect_protocol.bus_initiator_socket.bind(
      bus.manager_target_sockets[1]);
  // memory controller
  bus.subordinate_initiator_sockets[1].bind(memory_controller.tsocket);
  // RAM
  memory_controller.isocket.bind(ram.tsocket);

  // IRQ
  interconnect_protocol.irq_initiator_sockets[0].bind(core.irq_socket);

  // interconnect protocol <-> interconnects
  for (unsigned int i = 0; i < num_interconnects; ++i) {
    interconnect_protocol.interconnect_initiator_sockets[i].bind(
        interconnects[i]->protocol_target_socket);

    interconnects[i]->protocol_initiator_socket.bind(
        interconnect_protocol.interconnect_target_sockets[i]);
  }

  // trackers
  // utilization
  utilization_trackers.push_back(&core.utilization_tracker);
  utilization_trackers.push_back(&cache.utilization_tracker);
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