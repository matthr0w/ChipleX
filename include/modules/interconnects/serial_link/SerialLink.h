#pragma once

#include <systemc>
#include <tlm>

#include "common/System.h"

#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/serial_link/ChannelAllocator.h"
#include "modules/interconnects/serial_link/DataLinkLayer.h"
#include "modules/interconnects/serial_link/NetworkLayer.h"
#include "modules/interconnects/serial_link/StreamFifo.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(SerialLink), public InterconnectBase {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const ChipletConfig chiplet_config;
  const InterconnectConfig interconnect_config;
  const unsigned num_cores;
  const unsigned num_links;

public:
  SerialLink(sc_module_name name, unsigned chiplet_id,
             ChipletConfig chiplet_config,
             InterconnectConfig interconnect_config, unsigned num_cores);
  ~SerialLink();

  simple_initiator_socket_tagged<SerialLink> *irq_sockets;

  // InterconnectBase
  void bind_clock(sc_clock & clk) override;

private:
  SLNetworkLayer network_layer;
  SLDataLinkLayer datalink_layer;
  std::vector<SLChannelAllocater *> channel_allocaters;

  StreamFifo stream_fifo_out;
  StreamFifo stream_fifo_in;

  unsigned compute_fifo_depth();
};