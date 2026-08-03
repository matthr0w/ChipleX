#include "modules/interconnects/serial_link/SerialLink.h"

SerialLink::SerialLink(sc_module_name name, unsigned chiplet_id, ChipletConfig chiplet_config, unsigned interconnect_id,
                       InterconnectConfig interconnect_config)
    : InterconnectBase(chiplet_id, chiplet_config, interconnect_id, interconnect_config),
      sc_module(name),
      network_layer("network_layer", chiplet_id, interconnect_id, interconnect_config, num_links, num_cores, axi_width),
      datalink_layer("data_link_layer", chiplet_id, interconnect_id, interconnect_config, num_links, num_cores,
                     axi_width),
      stream_fifo_out("stream_fifo_out", 2),
      stream_fifo_in("stream_fifo_in", compute_fifo_depth()) {
	for (unsigned link_id = 0; link_id < num_links; ++link_id) {
		std::string name = "channel_allocater" + std::to_string(link_id);
		channel_allocaters.push_back(
		    new SLChannelAllocater(name.c_str(), link_id, interconnect_config, num_links, num_cores, axi_width));
	}

	network_layer.stream_fifo_in(stream_fifo_in);
	network_layer.stream_fifo_out(stream_fifo_out);
	datalink_layer.stream_fifo_in(stream_fifo_in);
	datalink_layer.stream_fifo_out(stream_fifo_out);

	for (unsigned i = 0; i < num_links; ++i) {
		datalink_layer.data_out_isockets[i].bind(channel_allocaters[i]->data_out_tsocket);
		channel_allocaters[i]->data_in_isocket.bind(datalink_layer.data_in_tsockets[i]);
	}

	// Register ports in InterconnectBase
	axi_in_port  = &network_layer.axi_in;
	axi_out_port = &network_layer.axi_out;
	for (unsigned i = 0; i < num_links; ++i) {
		link_in_ports[i]  = &channel_allocaters[i]->data_in_tsocket;
		link_out_ports[i] = &channel_allocaters[i]->data_out_isocket;
	}
	for (unsigned i = 0; i < num_cores; ++i) {
		irq_ports[i] = &network_layer.irq_sockets[i];
	}
}

SerialLink::~SerialLink() {
	for (auto *channel_allocater : channel_allocaters) {
		delete channel_allocater;
	}
	channel_allocaters.clear();
}

unsigned SerialLink::compute_fifo_depth() {
	bool     ddr          = interconnect_config.node["phy"]["ddr"].as<bool>();
	unsigned num_channels = interconnect_config.node["phy"]["num_channels"].as<unsigned>();
	unsigned num_lanes    = interconnect_config.node["phy"]["num_lanes"].as<unsigned>();
	unsigned num_credits  = interconnect_config.node["phy"]["num_credits"].as<unsigned>();

	unsigned bandwidth = num_channels * num_lanes * (ddr ? 2 : 1);

	unsigned payload_splits =
	    (Payload_t::simulation_size(chiplet_config.node["axi"]["width"].as<unsigned>()) * 8 + bandwidth - 1) /
	    bandwidth;

	return num_credits * payload_splits;
}

void SerialLink::bind_clocks(Clocks &clocks) {
	network_layer.clk.bind(clocks.get("protocol"));
	datalink_layer.clk.bind(clocks.get("protocol"));
}