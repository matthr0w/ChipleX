#include "FPGA.h"

#include "include/globals.h"

using namespace sc_core;
using namespace tlm;

FPGA::FPGA(sc_module_name name)
    : sc_module(name), generator("Generator", 0), ram("RAM"), bus("Bus", 0),
      interconnectprotocol("InterconnectProtocol", 0) {
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    std::string name =
        "Interconnect" +
        std::to_string(i + 1); // interconnect names follow chiplet ids
    interconnects.push_back(new fpga::Interconnect(
        name.c_str(),
        interconnect_config.get<double>("interconnect.bandwidth_fpga"),
        fpga_distance_mm));
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
  // generator
  generator.socket.bind(bus.generator_target_socket);
  // RAM
  bus.ram_initiator_socket.bind(ram.socket);
  // interconnects
  bus.interconnect_initiator_socket.bind(
      interconnectprotocol.bus_target_socket);
  interconnectprotocol.bus_initiator_socket.bind(
      bus.interconnect_target_socket);

  // IRQ
  interconnectprotocol.generator_irq_initiator_socket.bind(
      generator.irq_socket);

  // interconnect protocol <-> interconnects
  for (unsigned int i = 0; i < num_chiplets; ++i) {
    interconnectprotocol.interconnect_initiator_sockets[i].bind(
        interconnects[i]->protocol_target_socket);

    interconnects[i]->protocol_initiator_socket.bind(
        interconnectprotocol.interconnect_target_sockets[i]);
  }
}