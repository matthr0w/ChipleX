#include "modules/interconnects/serial_link/SerialLink.h"

#include "common/System.h"

SerialLink::SerialLink(sc_module_name name, unsigned chiplet_id,
                       ChipletConfig chiplet_config,
                       InterconnectConfig interconnect_config,
                       unsigned num_cores)
    : InterconnectBase(num_cores, chiplet_config.connections.size()),
      sc_module(name), chiplet_config(chiplet_config),
      interconnect_config(interconnect_config), num_cores(num_cores),
      num_links(chiplet_config.connections.size()),
      network_layer("network_layer", chiplet_id, chiplet_config,
                    interconnect_config, num_cores),
      datalink_layer("data_link_layer", chiplet_id, chiplet_config,
                     interconnect_config),
      stream_fifo_out("stream_fifo_out", 2),
      stream_fifo_in("stream_fifo_in", compute_fifo_depth()) {
  for (int i = 0; i < num_links; ++i) {
    std::string name = "channel_allocater" + std::to_string(i);
    channel_allocaters.push_back(
        new SLChannelAllocater(name.c_str(), i, chiplet_config));
  }

  irq_sockets = new simple_initiator_socket_tagged<SerialLink>[num_cores];

  network_layer.stream_fifo_in(stream_fifo_in);
  network_layer.stream_fifo_out(stream_fifo_out);
  datalink_layer.stream_fifo_in(stream_fifo_in);
  datalink_layer.stream_fifo_out(stream_fifo_out);

  for (unsigned i = 0; i < num_links; ++i) {
    datalink_layer.data_out_isockets[i].bind(
        channel_allocaters[i]->data_out_tsocket);
    channel_allocaters[i]->data_in_isocket.bind(
        datalink_layer.data_in_tsockets[i]);
  }

  // Register ports in InterconnectBase
  axi_in_port =
      reinterpret_cast<ARM::AXI::SimpleTargetSocket<InterconnectBase> *>(
          &network_layer.axi_in);
  axi_out_port =
      reinterpret_cast<ARM::AXI::SimpleInitiatorSocket<InterconnectBase> *>(
          &network_layer.axi_out);
  for (unsigned i = 0; i < num_links; ++i) {
    link_in_ports[i] =
        reinterpret_cast<simple_target_socket_tagged<InterconnectBase> *>(
            &channel_allocaters[i]->data_in_tsocket);
    link_out_ports[i] =
        reinterpret_cast<simple_initiator_socket_tagged<InterconnectBase> *>(
            &channel_allocaters[i]->data_out_isocket);
  }
  for (int i = 0; i < num_cores; ++i)
    irq_ports[i] =
        reinterpret_cast<simple_initiator_socket_tagged<InterconnectBase> *>(
            &irq_sockets[i]);
}

SerialLink::~SerialLink() {
  for (auto *channel_allocater : channel_allocaters) {
    delete channel_allocater;
  }
  channel_allocaters.clear();

  delete[] irq_sockets;
}

unsigned SerialLink::compute_fifo_depth() {
  bool ddr = interconnect_config.config["ddr"].as<bool>();
  unsigned num_channels =
      interconnect_config.config["num_channels"].as<unsigned>();
  unsigned num_lanes = interconnect_config.config["num_lanes"].as<unsigned>();
  unsigned num_credits =
      interconnect_config.config["num_credits"].as<unsigned>();

  unsigned bandwidth = num_channels * num_lanes * (ddr ? 2 : 1);

  unsigned payload_splits =
      (Payload_t::simulation_size(
           chiplet_config.config["axi"]["width"].as<unsigned>()) *
           8 +
       bandwidth - 1) /
      bandwidth;

  return num_credits * payload_splits;
}

void SerialLink::bind_clock(sc_clock &clk) {
  network_layer.clk.bind(clk);
  datalink_layer.clk.bind(clk);
}