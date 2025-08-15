#include "Chiplet.h"

#include "include/globals.h"

unsigned int Chiplet::instance = 1; // id == 0 is reserved for FPGA

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++),
      core0("Core0", chiplet_id, 0, interconnect_irq_delay),
      core1("Core1", chiplet_id, 1, interconnect_irq_delay),
      cache0("Cache0", chiplet_id, chiplet_cache_size, chiplet_cache_block_size,
             chiplet_cache_arbitration_delay, chiplet_cache_access_delay,
             chiplet_bus_width, chiplet_bus_clk_cycle),
      cache1("Cache1", chiplet_id, chiplet_cache_size, chiplet_cache_block_size,
             chiplet_cache_arbitration_delay, chiplet_cache_access_delay,
             chiplet_bus_width, chiplet_bus_clk_cycle),
      bus("Bus", chiplet_id, num_bus_managers, num_bus_subordinates,
          chiplet_bus_arbitration_delay),
      interconnectprotocol("InterconnectProtocol", chiplet_id, num_cores,
                           num_interconnects, interconnect_flit_size,
                           interconnect_overhead_size, interconnect_pre_delay,
                           interconnect_post_delay, interconnect_irq_delay,
                           chiplet_bus_width, chiplet_bus_clk_cycle),
      memorycontroller_axi("MemoryControllerAXI", 32, 32, sc_time(5, SC_NS),
                           sc_time(5, SC_NS)),
      memorycontroller("MemoryController", chiplet_bus_width,
                       chiplet_bus_clk_cycle, chiplet_ram_size),
      ram("RAM", chiplet_ram_size, chiplet_ram_width, chiplet_ram_clk_cycle,
          chiplet_ram_address_delay, chiplet_ram_access_delay) {
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
  core0.socket.bind(cache0.core_target_socket);
  core1.socket.bind(cache1.core_target_socket);
  // caches
  dummy_initiator0.bind(bus.manager_target_sockets[0]);
  dummy_initiator1.bind(bus.manager_target_sockets[1]);
  cache0.bus_initiator_socket.bind(memorycontroller_axi.read_target_socket);
  cache1.bus_initiator_socket.bind(memorycontroller_axi.read_target_socket);
  // interconnects
  bus.subordinate_initiator_sockets[0].bind(
      interconnectprotocol.bus_target_socket);
  interconnectprotocol.bus_initiator_socket.bind(bus.manager_target_sockets[2]);
  // memory controller
  bus.subordinate_initiator_sockets[1].bind(dummy_target);
  memorycontroller_axi.read_initiator_socket.bind(
      memorycontroller.bus_target_socket);
  // RAM
  memorycontroller.ram_initiator_socket.bind(ram.socket);

  // IRQs
  interconnectprotocol.irq_initiator_sockets[0].bind(core0.irq_socket);
  interconnectprotocol.irq_initiator_sockets[1].bind(core1.irq_socket);

  // interconnect protocol <-> interconnects
  for (unsigned int i = 0; i < 3; ++i) {
    interconnectprotocol.interconnect_initiator_sockets[i].bind(
        interconnects[i]->protocol_target_socket);

    interconnects[i]->protocol_initiator_socket.bind(
        interconnectprotocol.interconnect_target_sockets[i]);
  }

  // trackers
  // utilization
  utilization_trackers.push_back(&core0.utilization_tracker);
  utilization_trackers.push_back(&core1.utilization_tracker);
  utilization_trackers.push_back(&cache0.utilization_tracker);
  utilization_trackers.push_back(&cache1.utilization_tracker);
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