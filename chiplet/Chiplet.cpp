#include "Chiplet.h"

#include "include/globals.h"

unsigned int Chiplet::instance = 1;

Chiplet::Chiplet(sc_module_name name)
    : sc_module(name), chiplet_id(Chiplet::instance++), bus("Bus", chiplet_id),
      core0("Core0", chiplet_id, 0), core1("Core1", chiplet_id, 1),
      interconnectprotocol("InterconnectProtocol", chiplet_id),
      memorycontroller("MemoryController"), ram("RAM") {
  for (unsigned int i = 0; i < 3; ++i) {
    std::string name = "Interconnect" + std::to_string(i);

    if (i == 0) { // to FPGA interconnect
      interconnects.push_back(new chiplet::Interconnect(
          name.c_str(), bandwidth_fpga, fpga_distance_mm));
    } else {
      interconnects.push_back(new chiplet::Interconnect(
          name.c_str(), bandwidth_chiplet, chiplet_distance_um / 1000));
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
  core0.socket.bind(bus.core0_target_socket);
  core1.socket.bind(bus.core1_target_socket);
  // interconnects
  bus.interconnect_initiator_socket.bind(
      interconnectprotocol.bus_target_socket);
  interconnectprotocol.bus_initiator_socket.bind(
      bus.interconnect_target_socket);
  // memory controller
  bus.ram_initiator_socket.bind(memorycontroller.bus_target_socket);
  // RAM
  memorycontroller.ram_initiator_socket.bind(ram.socket);

  // IRQs
  interconnectprotocol.core0_irq_initiator_socket.bind(core0.irq_socket);
  interconnectprotocol.core1_irq_initiator_socket.bind(core1.irq_socket);

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
  utilization_trackers.push_back(&bus.utilization_tracker);
  utilization_trackers.push_back(&memorycontroller.utilization_tracker);
  utilization_trackers.push_back(&ram.utilization_tracker);
  utilization_trackers.push_back(&interconnectprotocol.utilization_tracker);

  for (unsigned int i = 0; i < 3; ++i) {
    utilization_trackers.push_back(&interconnects[i]->utilization_tracker);
  }

  // buffer usages
  for (unsigned int i = 0; i < 3; ++i) {
    buffer_trackers.push_back(&interconnects[i]->tx_tracker);
    buffer_trackers.push_back(&interconnects[i]->rx_tracker);
  }
}