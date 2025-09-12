#pragma once

#include <systemc>
#include <tlm>

#include "modules/interconnects/Base.h"
#include "modules/interconnects/serial_link/DataLinkLayer.h"
#include "modules/interconnects/serial_link/NetworkLayer.h"
#include "modules/interconnects/serial_link/Types.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(SerialLink), public InterconnectBase {
public:
  SerialLink(sc_module_name name, unsigned chip_id, unsigned axi_width,
             unsigned num_cores, int num_credits);

  simple_initiator_socket_tagged<SerialLink> *irq_sockets;

  // InterconnectBase
  void bind_axi(AXIBus & bus, sc_clock & clk) override;
  void bind_core(unsigned index, Core &core) override;

private:
  sc_signal<bool> rst;

  SLNetworkLayer network_layer;
  SLDataLinkLayer datalink_layer;
  
  sc_fifo<Payload_t *> stream_fifo_out;
};