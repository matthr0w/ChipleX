#include "modules/interconnects/serial_link/SerialLink.h"

#include "globals.h"

SerialLink::SerialLink(sc_module_name name, unsigned chip_id,
                       unsigned axi_width, unsigned num_cores,
                       unsigned num_interconnects, int num_credits)
    : InterconnectBase(1), sc_module(name), num_cores(num_cores),
      num_interconnects(num_interconnects),
      network_layer("NetworkLayer", chip_id, axi_width, num_interconnects,
                    num_credits),
      datalink_layer("DataLinkLayer", chip_id, axi_width, num_interconnects),
      stream_fifo_out("StreamFifoOut", 2),
      stream_fifo_in("StreamFifoIn", 2) // TODO: Fix stream fifo depth
{
  for (unsigned int i = 0; i < num_interconnects; ++i) {
    std::string name = "ChannelAllocater" + std::to_string(i);
    // TODO: Update distance
    channel_allocaters.push_back(new SLChannelAllocater(
        name.c_str(), axi_width, chiplet_distance_um / 1000));
  }

  irq_sockets = new simple_initiator_socket_tagged<SerialLink>[num_cores];

  network_layer.stream_fifo_in(stream_fifo_in);
  network_layer.stream_fifo_out(stream_fifo_out);
  datalink_layer.stream_fifo_in(stream_fifo_in);
  datalink_layer.stream_fifo_out(stream_fifo_out);

  for (unsigned i = 0; i < num_interconnects; ++i) {
    datalink_layer.data_out_isockets[i].bind(
        channel_allocaters[i]->data_out_tsocket);
    channel_allocaters[i]->data_in_isocket.bind(
        datalink_layer.data_in_tsockets[i]);

    in_ports[i] =
        reinterpret_cast<simple_target_socket_tagged<InterconnectBase> *>(
            &channel_allocaters[i]->data_in_tsocket);
    out_ports[i] =
        reinterpret_cast<simple_initiator_socket_tagged<InterconnectBase> *>(
            &channel_allocaters[i]->data_out_isocket);
  }
}

SerialLink::~SerialLink() {
  for (auto *channel_allocater : channel_allocaters) {
    delete channel_allocater;
  }
  channel_allocaters.clear();

  delete[] irq_sockets;
}

void SerialLink::bind_axi(AXIBus &bus, sc_clock &clk) {
  network_layer.clk.bind(clk);
  datalink_layer.clk.bind(clk);

  bus.sub_isockets[1]->bind(network_layer.axi_in);
  network_layer.axi_out.bind(*bus.mgr_tsockets[num_cores]);
}

void SerialLink::bind_core(unsigned index, Core &core) {
  // TODO: Bind interrupt lines
  irq_sockets[index].bind(core.irq_socket);
}