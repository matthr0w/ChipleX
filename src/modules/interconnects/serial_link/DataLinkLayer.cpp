#include "modules/interconnects/serial_link/DataLinkLayer.h"

#include "ARM/TLM/arm_axi4.h"
#include "common/RoutingTable.h"

#include "logging.h"

SLDataLinkLayer::SLDataLinkLayer(sc_module_name name, unsigned chip_id)
    : sc_module(name), chip_id(chip_id) {
  SC_METHOD(clk_posedge);
  dont_initialize();
  sensitive << clk.pos();
}

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

void SLDataLinkLayer::clk_posedge() {
  int depth = stream_fifo_in->num_available();
  if (depth == 0)
    return;

  SC_LOG_DEBUG(this, "# Stream FIFO Contents");

  for (int i = 0; i < depth; ++i) {
    Payload_t *payload = stream_fifo_in->peek(i);
    if (payload) {
      int route = RoutingTable::get_route(
          chip_id, UserSignals::decode(payload->user).destination);
      SC_LOG_DEBUG(this, "Route: " << route);
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
    }
  }
}