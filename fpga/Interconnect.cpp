#include "Interconnect.h"

#include "common/Delays.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

fpga::Interconnect::Interconnect(sc_module_name name, double bandwidth,
                                 double distance)
    : sc_module(name), utilization_tracker(this->name()),
      tx_tracker(this->name() + std::string(" Tx Buffer")),
      rx_tracker(this->name() + std::string(" Rx Buffer")),
      bandwidth(bandwidth), distance(distance),
      protocol_target_socket("protocol_target_socket"),
      protocol_initiator_socket("protocol_initiator_socket"),
      interconnect_target_socket("interconnect_target_socket"),
      interconnect_initiator_socket("interconnect_initiator_socket"),
      peq_protocol("peq_protocol"), peq_interconnect("peq_interconnect"),
      tx_buffer_used_bytes(0), rx_buffer_used_bytes(0) {
  protocol_target_socket.register_nb_transport_fw(
      this, &fpga::Interconnect::nb_transport_fw_protocol);
  protocol_initiator_socket.register_nb_transport_bw(
      this, &fpga::Interconnect::nb_transport_bw_protocol);

  interconnect_target_socket.register_nb_transport_fw(
      this, &fpga::Interconnect::nb_transport_fw_interconnect);
  interconnect_initiator_socket.register_nb_transport_bw(
      this, &fpga::Interconnect::nb_transport_bw_interconnect);

  SC_THREAD(process_protocol_transaction);
  sensitive << peq_protocol.get_event();
  SC_THREAD(process_interconnect_transaction);
  sensitive << peq_interconnect.get_event();

  SC_THREAD(process_tx_buffer);
  SC_THREAD(process_rx_buffer);
}

void fpga::Interconnect::process_protocol_transaction() {
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq_protocol.get_next_transaction();

    // put transaction in tx buffer
    auto *transaction_copy =
        static_cast<ChipletPayload *>(transaction)->clone();
    tx_buffer_used_bytes += flit_size;
    tx_tracker.update(tx_buffer_used_bytes);
    tx_buffer.push_back(transaction_copy);
    tx_buffer_in_event.notify();

    // begin response to protocol layer
    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp =
        protocol_target_socket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }
  }
}

void fpga::Interconnect::process_interconnect_transaction() {
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq_interconnect.get_next_transaction();

    // put transaction in rx buffer
    auto *transaction_copy =
        static_cast<ChipletPayload *>(transaction)->clone();
    rx_buffer_used_bytes += flit_size;
    rx_tracker.update(rx_buffer_used_bytes);
    rx_buffer.push_back(transaction_copy);
    rx_buffer_in_event.notify();

    // begin response to interconnect
    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp =
        interconnect_target_socket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }
  }
}

void fpga::Interconnect::process_tx_buffer() {
  while (true) {
    wait(tx_buffer_in_event);

    while (!tx_buffer.empty()) {
      utilization_tracker.set_active();

      tlm_generic_payload *transaction = tx_buffer.front();
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;
      tlm_sync_enum tlm_resp;

      tlm_resp = interconnect_initiator_socket->nb_transport_fw(*transaction,
                                                                phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }

      wait(interconnect_transaction_done);

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

void fpga::Interconnect::process_rx_buffer() {
  while (true) {
    wait(rx_buffer_in_event);

    while (!rx_buffer.empty()) {
      utilization_tracker.set_active();

      tlm_generic_payload *transaction = rx_buffer.front();
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;
      tlm_sync_enum tlm_resp;

      tlm_resp = protocol_initiator_socket->nb_transport_fw(*transaction, phase,
                                                            delay);

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

      utilization_tracker.set_idle();
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
fpga::Interconnect::nb_transport_fw_protocol(tlm_generic_payload &transaction,
                                             tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "PROTOCOL: Received request from Protocol Layer");

  if (tx_buffer_used_bytes + flit_size > buffer_size) {
    SC_LOG_WARN(this, transaction, "Tx buffer full -> waiting...");
    wait(tx_buffer_out_event);
  }

  // add protocol layer to interconnect process delay
  delay +=
      get_protocol2interconnect_process_delay(*this, transaction, pre_delay);

  peq_protocol.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum fpga::Interconnect::nb_transport_fw_interconnect(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "PROTOCOL: Received request from Interconnect");

  if (rx_buffer_used_bytes + flit_size > buffer_size) {
    SC_LOG_WARN(this, transaction, "Rx buffer full -> waiting...");
    wait(rx_buffer_out_event);
  }

  // add die to die transfer delay
  delay += get_die2die_transfer_delay(*this, transaction, bandwidth, distance,
                                      flit_size);

  peq_interconnect.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum
fpga::Interconnect::nb_transport_bw_protocol(tlm_generic_payload &transaction,
                                             tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction,
                 "PROTOCOL: Received response from Protocol Layer");

    protocol_transaction_done.notify(SC_ZERO_TIME);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum fpga::Interconnect::nb_transport_bw_interconnect(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction,
                 "PROTOCOL: Received response from Interconnect");

    interconnect_transaction_done.notify(SC_ZERO_TIME);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}