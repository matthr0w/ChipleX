#include "modules/interconnects/serial_link/SerialLink.h"
#include "modules/interconnects/serial_link/NetworkLayer.h"

const char *tag_to_str(Tag_e tag) {
  switch (tag) {
  case TagIdle:
    return "TagIdle";
  case TagAW:
    return "TagAW";
  case TagW:
    return "TagW";
  case TagAR:
    return "TagAR";
  case TagR:
    return "TagR";
  default:
    return "Unknown";
  }
}

SerialLink::SerialLink(sc_module_name name, unsigned chip_id,
                       unsigned axi_width, unsigned num_cores, int num_credits)
    : InterconnectBase(1), sc_module(name),
      network_layer("NetworkLayer", axi_width, num_credits),
      stream_fifo_out("stream_fifo_out", 2) {
  irq_sockets = new simple_initiator_socket_tagged<SerialLink>[num_cores];
  rst.write(true);
  network_layer.stream_fifo_out(stream_fifo_out);

  SC_THREAD(fifo_monitor_thread);
  sensitive << clk_in.pos();
}

void SerialLink::bind_axi(AXIBus &bus, sc_clock &clk) {
  clk_in.bind(clk);

  // TODO: Bind reset signals
  network_layer.clk.bind(clk);
  network_layer.rst_n.bind(rst);

  bus.sub_isockets[1]->bind(network_layer.axi_in);
}

void SerialLink::bind_core(unsigned index, Core &core) {
  // TODO: Bind interrupt lines
  irq_sockets[index].bind(core.irq_socket);
}

void SerialLink::fifo_monitor_thread() {
  while (true) {
    wait();

    int depth = stream_fifo_out.num_available();
    if (depth == 0)
      continue;

    std::cout << "###### STREAM FIFO CONTENT ######" << std::endl;

    for (int i = 0; i < depth; ++i) {
      Payload_t *payload = nullptr;
      if (stream_fifo_out.nb_read(payload) && payload) {
        std::ostringstream msg;
        msg << tag_to_str(payload->hdr) << ", addr=0x" << std::hex
            << payload->axi_ch.addr << ", data=[";
        for (size_t j = 0; j < payload->axi_ch.data.size(); ++j) {
          if (j != 0)
            msg << " ";
          msg << std::hex << std::setw(2) << std::setfill('0')
              << (int)payload->axi_ch.data[j];
        }
        msg << "], credit=" << std::dec << payload->credit
            << ", b_valid=" << payload->b_valid << ", user=";
        for (int u = 63; u >= 0; --u) {
          msg << ((payload->user >> u) & 1ULL);
          if (u % 8 == 0)
            msg << " ";
        }
        std::cout << msg.str() << std::endl;
        ;
      }
      delete payload;
    }

    std::cout << "################################" << std::endl;
  }
}