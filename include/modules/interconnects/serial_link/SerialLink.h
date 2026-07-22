#pragma once

#include <systemc>
#include <tlm>

#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/serial_link/ChannelAllocator.h"
#include "modules/interconnects/serial_link/DataLinkLayer.h"
#include "modules/interconnects/serial_link/NetworkLayer.h"
#include "modules/interconnects/serial_link/StreamFifo.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(SerialLink), public InterconnectBase {
  public:
	SerialLink(sc_module_name name, unsigned chiplet_id, ChipletConfig chiplet_config, unsigned interconnect_id,
	           InterconnectConfig interconnect_config);
	~SerialLink();

	// InterconnectBase
	void bind_clocks(Clocks & clocks) override;

  private:
	SLNetworkLayer                    network_layer;
	SLDataLinkLayer                   datalink_layer;
	std::vector<SLChannelAllocater *> channel_allocaters;

	StreamFifo stream_fifo_out;
	StreamFifo stream_fifo_in;

	unsigned compute_fifo_depth();
};