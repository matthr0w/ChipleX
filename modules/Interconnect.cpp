#include "Interconnect.h"

#include "common/Tracker.h"

Interconnect::Interconnect(sc_module_name name, unsigned int buffer_size,
                           unsigned int flit_size, double bandwidth,
                           double distance)
    : sc_module(name), buffer_size(buffer_size), flit_size(flit_size),
      bandwidth(bandwidth), distance(distance),
      utilization_tracker(this->name()),
      tx_tracker(this->name() + std::string(" Tx Buffer")),
      rx_tracker(this->name() + std::string(" Rx Buffer")), incoming_flits(0),
      protocol_peq("protocol_peq"), phy_peq("phy_peq"), tx_buffer_used_bytes(0),
      rx_buffer_used_bytes(0) {
  protocol_tsocket.register_nb_transport_fw(
      this, &Interconnect::nb_transport_fw_protocol);
  protocol_isocket.register_nb_transport_bw(
      this, &Interconnect::nb_transport_bw_protocol);

  phy_tsocket.register_nb_transport_fw(this,
                                       &Interconnect::nb_transport_fw_phy);
  phy_isocket.register_nb_transport_bw(this,
                                       &Interconnect::nb_transport_bw_phy);

  SC_THREAD(process_protocol_transaction);
  sensitive << protocol_peq.get_event();
  SC_THREAD(process_phy_transaction);
  sensitive << phy_peq.get_event();

  SC_THREAD(process_tx_buffer);
  SC_THREAD(process_rx_buffer);
}

void Interconnect::process_protocol_transaction() {
  while (true) {
    wait();

    tlm_generic_payload *transaction = protocol_peq.get_next_transaction();

    // put transaction in tx buffer
    auto *transaction_copy =
        static_cast<ChipletPayload *>(transaction)->clone();
    tx_buffer_used_bytes += flit_size;
    tx_tracker.update(tx_buffer_used_bytes);
    tx_buffer.push_back(transaction_copy);
    tx_buffer_in_event.notify();

    tlm_phase phase = BEGIN_RESP;
    sc_time delay = SC_ZERO_TIME;

    tlm_sync_enum tlm_resp =
        protocol_tsocket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }
  }
}

void Interconnect::process_phy_transaction() {
  while (true) {
    wait();

    tlm_generic_payload *transaction = phy_peq.get_next_transaction();
    ChipletExtension *ext = transaction->get_extension<ChipletExtension>();

    // only put transaction in rx buffer if transfer was successful
    if (ext->success) {
      auto *transaction_copy =
          static_cast<ChipletPayload *>(transaction)->clone();
      rx_buffer_used_bytes += flit_size;
      rx_tracker.update(rx_buffer_used_bytes);
      rx_buffer.push_back(transaction_copy);
      rx_buffer_in_event.notify();
      ++incoming_flits;
    }

    tlm_phase phase = BEGIN_RESP;
    sc_time delay = SC_ZERO_TIME;

    tlm_sync_enum tlm_resp =
        phy_tsocket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }
  }
}

void Interconnect::process_tx_buffer() {
  while (true) {
    wait(tx_buffer_in_event);

    while (!tx_buffer.empty()) {
      utilization_tracker.set_active();

      tlm_generic_payload *transaction = tx_buffer.front();
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;
      tlm_sync_enum tlm_resp;

      tlm_resp = phy_isocket->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }

      wait(phy_transaction_done);

      // remove from tx buffer
      tx_buffer_used_bytes -= flit_size;
      tx_tracker.update(tx_buffer_used_bytes);
      tx_buffer.pop_front();
      tx_buffer_out_event.notify();

      delete transaction;

      utilization_tracker.set_idle();
    }
  }
}

void Interconnect::process_rx_buffer() {
  while (true) {
    wait(rx_buffer_in_event);

    while (!rx_buffer.empty()) {
      utilization_tracker.set_idle();

      tlm_generic_payload *transaction = rx_buffer.front();
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;
      tlm_sync_enum tlm_resp;

      tlm_resp = protocol_isocket->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }

      wait(protocol_transaction_done);

      // remove from rx buffer
      rx_buffer_used_bytes -= flit_size;
      rx_tracker.update(rx_buffer_used_bytes);
      rx_buffer.pop_front();
      rx_buffer_out_event.notify();

      delete transaction;
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
Interconnect::nb_transport_fw_protocol(tlm_generic_payload &transaction,
                                       tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase << " - Protocol");

  if (tx_buffer_used_bytes + flit_size > buffer_size) {
    SC_LOG_WARN(this, transaction, "Tx buffer full -> waiting...");
    wait(tx_buffer_out_event);
  }

  protocol_peq.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum
Interconnect::nb_transport_fw_phy(tlm_generic_payload &transaction,
                                  tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase << " - PHY");

  if (rx_buffer_used_bytes + flit_size > buffer_size) {
    SC_LOG_WARN(this, transaction, "Rx buffer full -> waiting...");
    wait(rx_buffer_out_event);
  }

  utilization_tracker.set_active();

  // delay += delays.pre_delay(transaction, flit_size);

  phy_peq.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum
Interconnect::nb_transport_bw_protocol(tlm_generic_payload &transaction,
                                       tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase << " - Protocol");

  switch (phase) {
  case BEGIN_RESP:
    protocol_transaction_done.notify(SC_ZERO_TIME);
    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum
Interconnect::nb_transport_bw_phy(tlm_generic_payload &transaction,
                                  tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase << " - PHY");

  switch (phase) {
  case BEGIN_RESP:
    phy_transaction_done.notify(delay);
    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}