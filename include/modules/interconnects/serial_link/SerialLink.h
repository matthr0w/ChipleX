#pragma once

#include <systemc>
#include <tlm>

#include "modules/interconnects/Base.h"
#include "modules/interconnects/serial_link/ChannelAllocator.h"
#include "modules/interconnects/serial_link/DataLinkLayer.h"
#include "modules/interconnects/serial_link/NetworkLayer.h"
#include "modules/interconnects/serial_link/StreamFifo.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(SerialLink), public InterconnectBase {
public:
  SerialLink(sc_module_name name, unsigned chip_id, unsigned axi_width,
             unsigned num_cores, unsigned num_interconnects,
             unsigned num_credits);
  ~SerialLink();

  simple_initiator_socket_tagged<SerialLink> *irq_sockets;

  // InterconnectBase
  void bind_axi(AXIBus & bus, sc_clock & clk) override;
  void bind_core(unsigned index, Core &core) override;

private:
  SLNetworkLayer network_layer;
  SLDataLinkLayer datalink_layer;
  std::vector<SLChannelAllocater *> channel_allocaters;

  StreamFifo stream_fifo_out;
  StreamFifo stream_fifo_in;

  unsigned compute_fifo_depth(unsigned axi_width, unsigned num_credits);

  // -------------------------------------------------------
  // Parameters
  // -------------------------------------------------------
  const unsigned num_cores;
  const unsigned num_interconnects;
};