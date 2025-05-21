#include "Interconnect.h"

#include "common/Delays.h"
#include "common/Flits.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

chiplet::Interconnect::Interconnect(sc_module_name name)
    : sc_module(name), protocol_target_socket("protocol_target_socket"),
      protocol_initiator_socket("protocol_initiator_socket"),
      interconnect_target_socket("interconnect_target_socket"),
      interconnect_initiator_socket("interconnect_initiator_socket"),
      tx_buffer_used_bytes(0), rx_buffer_used_bytes(0) {
  protocol_target_socket.register_nb_transport_fw(
      this, &chiplet::Interconnect::nb_transport_fw_protocol);
  protocol_initiator_socket.register_nb_transport_bw(
      this, &chiplet::Interconnect::nb_transport_bw_protocol);

  interconnect_target_socket.register_nb_transport_fw(
      this, &chiplet::Interconnect::nb_transport_fw_interconnect);
  interconnect_initiator_socket.register_nb_transport_bw(
      this, &chiplet::Interconnect::nb_transport_bw_interconnect);

  SC_THREAD(process_tx_buffer);
  SC_THREAD(process_rx_buffer);
}

void chiplet::Interconnect::process_tx_buffer() {
  while (true) {
    wait(tx_buffer_in_event);

    while (!tx_buffer.empty()) {
      tlm_generic_payload *transaction = tx_buffer.front();
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;

      unsigned int transaction_flit_size =
          get_flit_bytes(*transaction,
                         interconnect_config.get<unsigned int>(
                             "interconnect_protocol.flit_size"),
                         interconnect_config.get<unsigned int>(
                             "interconnect_protocol.header_size"));

      SC_LOG_DEBUG(this, *transaction, "Tx->Rx transmission started");
      tlm_sync_enum tlm_resp = interconnect_initiator_socket->nb_transport_fw(
          *transaction, phase, delay);

      if (tlm_resp == TLM_COMPLETED) {
        wait(delay);

        SC_LOG_DEBUG(this, *transaction, "Tx->Rx transmission finished");
      }

      // remove from tx buffer
      tx_buffer_used_bytes -= transaction_flit_size;
      tx_buffer.pop_front();
      tx_buffer_out_event.notify();

      delete transaction;
    }
  }
}

void chiplet::Interconnect::process_rx_buffer() {
  while (true) {
    wait(rx_buffer_in_event);

    while (!rx_buffer.empty()) {
      tlm_generic_payload *transaction = rx_buffer.front();
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;

      unsigned int transaction_flit_size =
          get_flit_bytes(*transaction,
                         interconnect_config.get<unsigned int>(
                             "interconnect_protocol.flit_size"),
                         interconnect_config.get<unsigned int>(
                             "interconnect_protocol.header_size"));

      SC_LOG_DEBUG(this, *transaction, "Rx->Protocol transmission started");
      tlm_sync_enum tlm_resp = protocol_initiator_socket->nb_transport_fw(
          *transaction, phase, delay);

      if (tlm_resp == TLM_COMPLETED) {
        wait(delay);

        SC_LOG_DEBUG(this, *transaction, "Rx->Protocol transmission finished");
      }

      // remove from rx buffer
      rx_buffer_used_bytes -= transaction_flit_size;
      rx_buffer.pop_front();
      rx_buffer_out_event.notify();

      delete transaction;
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum chiplet::Interconnect::nb_transport_fw_protocol(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "PROTOCOL: Received request from Protocol Layer");

  SC_LOG_DEBUG(this, transaction,
               "Tx buffer bytes: " << tx_buffer_used_bytes << "/"
                                   << interconnect_config.get<unsigned int>(
                                          "interconnect.buffer_size"));

  auto *transaction_copy = static_cast<ChipletPayload *>(&transaction)->clone();

  unsigned int transaction_flit_size = get_flit_bytes(
      *transaction_copy,
      interconnect_config.get<unsigned int>("interconnect_protocol.flit_size"),
      interconnect_config.get<unsigned int>(
          "interconnect_protocol.header_size"));

  if (tx_buffer_used_bytes + transaction_flit_size >
      interconnect_config.get<unsigned int>("interconnect.buffer_size")) {
    SC_LOG_WARN(this, transaction, "Tx buffer full -> waiting...");
    wait(tx_buffer_out_event);
  }

  // add protocol layer to interconnect process delay
  delay += get_protocol2interconnect_process_delay(
      *this, transaction,
      interconnect_config.get<sc_time>("interconnect_protocol.pre_delay"));

  // put transaction in tx buffer
  SC_LOG_DEBUG(this, transaction, "Write transaction in Tx buffer");
  tx_buffer_used_bytes += transaction_flit_size;
  tx_buffer.push_back(transaction_copy);
  tx_buffer_in_event.notify(delay);

  SC_LOG_DEBUG(this, transaction,
               "Tx buffer bytes: " << tx_buffer_used_bytes << "/"
                                   << interconnect_config.get<unsigned int>(
                                          "interconnect.buffer_size"));

  // begin response to protocol layer
  tlm_phase resp_phase = BEGIN_RESP;
  sc_time resp_delay = delay;

  protocol_target_socket->nb_transport_bw(transaction, resp_phase, resp_delay);

  phase = END_REQ;
  return TLM_COMPLETED;
}

tlm_sync_enum chiplet::Interconnect::nb_transport_fw_interconnect(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "PROTOCOL: Received request from Interconnect");

  SC_LOG_DEBUG(this, transaction,
               "Rx buffer bytes: " << rx_buffer_used_bytes << "/"
                                   << interconnect_config.get<unsigned int>(
                                          "interconnect.buffer_size"));

  auto *transaction_copy = static_cast<ChipletPayload *>(&transaction)->clone();

  unsigned int transaction_flit_size = get_flit_bytes(
      *transaction_copy,
      interconnect_config.get<unsigned int>("interconnect_protocol.flit_size"),
      interconnect_config.get<unsigned int>(
          "interconnect_protocol.header_size"));

  if (rx_buffer_used_bytes + transaction_flit_size >
      interconnect_config.get<unsigned int>("interconnect.buffer_size")) {
    SC_LOG_WARN(this, transaction, "Rx buffer full -> waiting...");
    wait(rx_buffer_out_event);
  }

  // add chiplet to chiplet transfer delay
  delay += get_chiplet2chiplet_transfer_delay(
      *this, transaction,
      interconnect_config.get<double>("interconnect.bandwidth"),
      interconnect_config.get<unsigned int>("interconnect_protocol.flit_size"),
      interconnect_config.get<unsigned int>(
          "interconnect_protocol.header_size"));

  // put transaction in rx buffer
  SC_LOG_DEBUG(this, transaction, "Write transaction in Rx buffer");
  rx_buffer_used_bytes += transaction_flit_size;
  rx_buffer.push_back(transaction_copy);
  rx_buffer_in_event.notify(delay);

  SC_LOG_DEBUG(this, transaction,
               "Rx buffer bytes: " << rx_buffer_used_bytes << "/"
                                   << interconnect_config.get<unsigned int>(
                                          "interconnect.buffer_size"));

  // begin response to interconnect
  tlm_phase resp_phase = BEGIN_RESP;
  sc_time resp_delay = delay;

  interconnect_target_socket->nb_transport_bw(transaction, resp_phase,
                                              resp_delay);

  phase = END_REQ;
  return TLM_COMPLETED;
}

tlm_sync_enum chiplet::Interconnect::nb_transport_bw_protocol(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction,
                 "PROTOCOL: Received response from Protocol Layer");

    rx_transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum chiplet::Interconnect::nb_transport_bw_interconnect(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction,
                 "PROTOCOL: Received response from Interconnect");

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}