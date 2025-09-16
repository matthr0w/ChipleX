#include "modules/interconnects/serial_link/DataLinkLayer.h"

#include "ARM/TLM/arm_axi4.h"
#include "common/RoutingTable.h"

#include "logging.h"

SLDataLinkLayer::SLDataLinkLayer(sc_module_name name, unsigned chip_id,
                                 unsigned axi_width, unsigned num_interconnects)
    : sc_module(name), chip_id(chip_id), axi_width(axi_width) {
  data_in_tsockets =
      new simple_target_socket_tagged<SLDataLinkLayer>[num_interconnects];
  data_out_isockets =
      new simple_initiator_socket_tagged<SLDataLinkLayer>[num_interconnects];

  for (unsigned int i = 0; i < num_interconnects; ++i) {
    data_in_tsockets[i].register_nb_transport_fw(
        this, &SLDataLinkLayer::nb_transport_fw, i);
    data_out_isockets[i].register_nb_transport_bw(
        this, &SLDataLinkLayer::nb_transport_bw, i);
  }

  SC_METHOD(clk_posedge);
  dont_initialize();
  sensitive << clk.pos();
}

SLDataLinkLayer::~SLDataLinkLayer() {
  delete[] data_in_tsockets;
  delete[] data_out_isockets;
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
  /* DATA IN */
  unsigned num_available = stream_fifo_in->num_available();
  if (num_available != 0) {
    SC_LOG_DEBUG(this, "# Stream FIFO Contents");
    for (int i = 0; i < num_available; ++i) {
      Payload_t *payload = stream_fifo_in->peek(i);
      if (payload) {
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

  /* DATA OUT */
  if (!data_out_ongoing) {
    Payload_t *payload = stream_fifo_out->peek();
    if (payload) {
      tlm_generic_payload *transaction = new tlm_generic_payload;
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;
      pack_payload(*transaction, *payload);

      int route = RoutingTable::get_route(
          chip_id, UserSignals::decode(payload->user).destination);

      tlm_sync_enum reply =
          data_out_isockets[route]->nb_transport_fw(*transaction, phase, delay);

      if (reply == TLM_UPDATED) {
        stream_fifo_out->read();
        data_out_ongoing = true;
      }
    }
  }
}

// -------------------------------------------------------
// Transport functions
// -------------------------------------------------------
tlm_sync_enum SLDataLinkLayer::nb_transport_fw(int id,
                                               tlm_generic_payload &transaction,
                                               tlm_phase &phase,
                                               sc_time &delay) {
  switch (phase) {
  case BEGIN_REQ: {
    if (!stream_fifo_in->reserve())
      return TLM_ACCEPTED;
    tlm_generic_payload *tptr = &transaction;
    sc_spawn([this, id, tptr, delay]() {
      wait(delay);
      Payload_t *payload = unpack_payload(*tptr);
      stream_fifo_in->write(payload);
      tlm_phase resp_phase = BEGIN_RESP;
      sc_time resp_delay = SC_ZERO_TIME;
      data_in_tsockets[id]->nb_transport_bw(*tptr, resp_phase, resp_delay);
    });
    phase = END_REQ;
    return TLM_UPDATED;
  }
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum SLDataLinkLayer::nb_transport_bw(int id,
                                               tlm_generic_payload &transaction,
                                               tlm_phase &phase,
                                               sc_time &delay) {
  switch (phase) {
  case BEGIN_RESP:
    data_out_ongoing = false;
    phase = END_RESP;
    delete &transaction;
    return TLM_UPDATED;
  }

  return TLM_ACCEPTED;
}

// -------------------------------------------------------
// Helper functions
// -------------------------------------------------------
void SLDataLinkLayer::pack_payload(tlm_generic_payload &transaction,
                                   Payload_t &payload) {
  auto *buf = new unsigned char[sizeof(Payload_t)];
  std::memcpy(buf, &payload, sizeof(Payload_t));
  transaction.set_data_ptr(buf);
  transaction.set_data_length(sizeof(Payload_t));
}

Payload_t *SLDataLinkLayer::unpack_payload(tlm_generic_payload &transaction) {
  auto *payload = new Payload_t(axi_width);
  std::memcpy(payload, transaction.get_data_ptr(), sizeof(Payload_t));
  return payload;
}