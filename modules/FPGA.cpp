#include "FPGA.h"

#include "include/globals.h"

using namespace sc_core;
using namespace tlm;

FPGA::FPGA(sc_module_name name)
    : sc_module(name), fpga_id(0), core("Core", fpga_id, 0, chiplet_ram_size,
                                        fpga_ram_size, interconnect_irq_delay),
      cache("Cache", fpga_id, fpga_cache_size, fpga_cache_block_size,
            fpga_cache_arbitration_delay, fpga_cache_access_delay,
            fpga_bus_width, fpga_bus_clk_cycle),
      bus("Bus", fpga_id, num_bus_masters, num_bus_slaves,
          fpga_bus_arbitration_delay),
      interconnectprotocol("InterconnectProtocol", fpga_id, num_cores,
                           num_interconnects, interconnect_flit_size,
                           interconnect_overhead_size, interconnect_pre_delay,
                           interconnect_post_delay, interconnect_irq_delay,
                           fpga_bus_width, fpga_bus_clk_cycle),
      memorycontroller("MemoryController", fpga_bus_width, fpga_bus_clk_cycle,
                       chiplet_ram_size),
      ram("RAM", fpga_ram_size, fpga_ram_width, fpga_ram_clk_cycle,
          fpga_ram_address_delay, fpga_ram_access_delay) {
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
  core.socket.bind(cache.core_target_socket);
  // cache
  cache.bus_initiator_socket.bind(bus.master_target_sockets[0]);
  // interconnects
  bus.slave_initiator_sockets[0].bind(interconnectprotocol.bus_target_socket);
  interconnectprotocol.bus_initiator_socket.bind(bus.master_target_sockets[1]);
  // memory controller
  bus.slave_initiator_sockets[1].bind(memorycontroller.bus_target_socket);
  // RAM
  memorycontroller.ram_initiator_socket.bind(ram.socket);

  // IRQ
  interconnectprotocol.irq_initiator_sockets[0].bind(core.irq_socket);

  // interconnect protocol <-> interconnects
  for (unsigned int i = 0; i < num_interconnects; ++i) {
    interconnectprotocol.interconnect_initiator_sockets[i].bind(
        interconnects[i]->protocol_target_socket);

    interconnects[i]->protocol_initiator_socket.bind(
        interconnectprotocol.interconnect_target_sockets[i]);
  }

  // trackers
  // utilization
  utilization_trackers.push_back(&core.utilization_tracker);
  utilization_trackers.push_back(&cache.utilization_tracker);
  utilization_trackers.push_back(&bus.utilization_tracker);
  utilization_trackers.push_back(&memorycontroller.utilization_tracker);
  utilization_trackers.push_back(&ram.utilization_tracker);
  utilization_trackers.push_back(&interconnectprotocol.utilization_tracker);

  for (unsigned int i = 0; i < num_interconnects; ++i) {
    utilization_trackers.push_back(&interconnects[i]->utilization_tracker);
  }

  // buffer usages
  for (unsigned int i = 0; i < num_interconnects; ++i) {
    buffer_trackers.push_back(&interconnects[i]->tx_tracker);
    buffer_trackers.push_back(&interconnects[i]->rx_tracker);
  }
}