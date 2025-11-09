#include "modules/interconnects/serial_link/DataLinkLayer.h"

#include "logging.h"

#include "common/Router.h"

SLDataLinkLayer::SLDataLinkLayer(sc_module_name name, unsigned chiplet_id,
                                 ChipletConfig chiplet_config,
                                 InterconnectConfig interconnect_config)
    : sc_module(name), chiplet_id(chiplet_id),
      num_links(chiplet_config.connections.size()),
      axi_width(chiplet_config.config["axi"]["width"].as<unsigned>()) {
  stats.register_utilization(this->name());

  data_in_tsockets =
      new simple_target_socket_tagged<SLDataLinkLayer>[num_links];
  data_out_isockets =
      new simple_initiator_socket_tagged<SLDataLinkLayer>[num_links];

  for (unsigned int i = 0; i < num_links; ++i) {
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

void SLDataLinkLayer::clk_posedge() {
  // Data out
  if (!data_out_ongoing) {
    Payload_t *payload = stream_fifo_out->peek();
    if (payload) {
      tlm_generic_payload *transaction = new tlm_generic_payload;
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;
      pack_payload(*transaction, *payload);

      uint8_t destination_id = UserSignals::decode(payload->user).dst_chiplet;
      int link_id = Router::instance().get_link_id(chiplet_id, destination_id);
      if (link_id == -1)
        SC_LOG_ERROR(this, "No valid routing path from "
                               << chiplet_id << " to " << int(destination_id));

      tlm_sync_enum reply = data_out_isockets[link_id]->nb_transport_fw(
          *transaction, phase, delay);

      if (reply == TLM_UPDATED) {
        stats.set_active(this->name());
        stats.increment_counter(this->name(), "transmission_count_out_link" +
                                                  std::to_string(link_id));
        stats.update_accum(this->name(),
                           "transmission_duration_out_us_link" +
                               std::to_string(link_id),
                           delay.to_seconds() * 1e6);
        stream_fifo_out->read();
        data_out_ongoing = true;
      }
    }
  }
}

// -------------------------------------------------------
// Transport Functions
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
      stats.set_active(this->name());
      wait(delay);
      stats.set_idle(this->name());
      stats.increment_counter(this->name(), "transmission_count_in_link" +
                                                std::to_string(id));

      Payload_t *payload = unpack_payload(*tptr);
      payload->link_id = id;
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
    stats.set_idle(this->name());
    data_out_ongoing = false;
    delete[] transaction.get_data_ptr();
    delete &transaction;

    phase = END_RESP;
    return TLM_UPDATED;
  }

  return TLM_ACCEPTED;
}

// -------------------------------------------------------
// Helper Functions
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