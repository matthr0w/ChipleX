#include "modules/interconnects/serial_link/SerialLink.h"

SerialLink::SerialLink(sc_module_name name, unsigned chip_id,
                       unsigned axi_width, unsigned num_cores, int num_credits)
    : InterconnectBase(1), sc_module(name),
      network_layer("NetworkLayer", axi_width, num_credits),
      datalink_layer("DataLinkLayer", chip_id),
      stream_fifo_out("StreamFifoOut", 2) {
  irq_sockets = new simple_initiator_socket_tagged<SerialLink>[num_cores];
  rst.write(true);
  network_layer.stream_fifo_out(stream_fifo_out);
  datalink_layer.stream_fifo_in(stream_fifo_out);
}

void SerialLink::bind_axi(AXIBus &bus, sc_clock &clk) {
  // TODO: Bind reset signals
  network_layer.clk.bind(clk);
  network_layer.rst_n.bind(rst);
  datalink_layer.clk.bind(clk);
  datalink_layer.rst_n.bind(rst);

  bus.sub_isockets[1]->bind(network_layer.axi_in);
}

void SerialLink::bind_core(unsigned index, Core &core) {
  // TODO: Bind interrupt lines
  irq_sockets[index].bind(core.irq_socket);
}