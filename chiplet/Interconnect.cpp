#include "Interconnect.h"
#include "Config.h"
#include "Delays.h"

#include <deque>

#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

Interconnect::Interconnect(sc_module_name name, unsigned int chiplet_id)
    : sc_module(name), chiplet_id(chiplet_id),
      bus_target_socket("bus_target_socket"),
      interconnect_target_socket("interconnect_target_socket"),
      bus_initiator_socket("bus_initiator_socket"),
      interconnect_initiator_socket("interconnect_initiator_socket"),
      core0_irq_initiator_socket("core0_irq_initiator_socket"),
      core1_irq_initiator_socket("core1_irq_initiator_socket") {

  bus_target_socket.register_nb_transport_fw(
      this, &Interconnect::nb_transport_fw_bus);
  bus_initiator_socket.register_nb_transport_bw(
      this, &Interconnect::nb_transport_bw_bus);

  interconnect_target_socket.register_nb_transport_fw(
      this, &Interconnect::nb_transport_fw_interconnect);
  interconnect_initiator_socket.register_nb_transport_bw(
      this, &Interconnect::nb_transport_bw_interconnect);

  SC_THREAD(process_tx_buffer);
  SC_THREAD(process_rx_buffer);
}

void Interconnect::process_tx_buffer() {
  while (true) {
    wait(tx_buffer_in_event);

    while (!tx_buffer.empty()) {
      tlm_generic_payload *transaction = tx_buffer.front();
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;

      SC_LOG_DEBUG(this, *transaction, "Tx->Rx transmission started");
      tlm_sync_enum tlm_resp = interconnect_initiator_socket->nb_transport_fw(
          *transaction, phase, delay);

      if (tlm_resp == TLM_COMPLETED) {
        wait(delay);

        SC_LOG_DEBUG(this, *transaction, "Tx->Rx transmission finished");
      }

      // remove from tx buffer
      tx_buffer.pop_front();
      tx_buffer_out_event.notify();

      delete transaction;
    }
  }
}

void Interconnect::process_rx_buffer() {
  while (true) {
    wait(rx_buffer_in_event);

    while (!rx_buffer.empty()) {
      tlm_generic_payload *transaction = rx_buffer.front();
      ChipletExtension *ext;
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;

      transaction->get_extension(ext);
      int request_id = ext->request_id;
      int source_id = ext->source_id;
      int core_id = ext->core_id;
      int destination_id = ext->destination_id;

      SC_LOG_DEBUG(this, *transaction, "Rx->Bus transmission started");
      tlm_sync_enum tlm_resp =
          bus_initiator_socket->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        // bus processes the request
        wait(delay);
      } else if (tlm_resp == TLM_ACCEPTED) {
        // bus accepted the request but is busy
      }

      wait(rx_transaction_done);

      SC_LOG_DEBUG(this, *transaction, "Rx->Bus transmission finished");

      // send IRQ to core
      if (source_id == chiplet_id &&
          transaction->get_command() == TLM_READ_COMMAND) {
        send_irq(*transaction);
      }

      // remove from rx buffer
      rx_buffer.pop_front();
      rx_buffer_out_event.notify();

      // check if destination has changed
      if (ext->destination_id != destination_id) {
        auto *transaction_copy =
            static_cast<ChipletPayload *>(transaction)->clone();
        // put transaction in tx buffer
        // TODO: handle tx buffer fill level
        SC_LOG_DEBUG(this, *transaction, "Write transaction in Tx buffer");
        tx_buffer.push_back(transaction_copy);
        tx_buffer_in_event.notify();
      }

      delete transaction;
    }
  }
}

void Interconnect::send_irq(tlm_generic_payload &transaction) {
  auto *irq = new ChipletPayload();
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  transaction.get_extension(ext);

  irq->set_command(TLM_IGNORE_COMMAND);
  irq->set_address(transaction.get_address());
  irq->set_data_length(transaction.get_data_length());

  irq->set_request_id(ext->request_id);
  irq->set_source_id(ext->source_id);
  irq->set_core_id(ext->core_id);
  irq->set_destination_id(ext->destination_id);

  SC_LOG_WARN(this, transaction, "Sending IRQ to Core" << ext->core_id);

  phase = BEGIN_REQ;
  delay = SC_ZERO_TIME;

  if (ext->core_id == 0) {
    tlm_resp = core0_irq_initiator_socket->nb_transport_fw(*irq, phase, delay);
  } else if (ext->core_id == 1) {
    tlm_resp = core1_irq_initiator_socket->nb_transport_fw(*irq, phase, delay);
  }

  if (tlm_resp == TLM_COMPLETED) {
    wait(delay);
  }

  delete irq;

  SC_LOG_WARN(this, transaction, "Sending IRQ to Core" << ext->core_id << " done");
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
Interconnect::nb_transport_fw_bus(tlm_generic_payload &transaction,
                                  tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "Received request from Bus");

  output_buffer_levels();

  auto *transaction_copy = static_cast<ChipletPayload *>(&transaction)->clone();

  if (tx_buffer.size() == INTERCONNECT_BUFFER_SIZE) {
    SC_LOG_WARN(this, transaction, "Tx buffer full -> waiting...");
    wait(tx_buffer_out_event);
  }

  // add bus transfer delay
  delay += get_bus_transfer_delay(*this, transaction);

  // put transaction in tx buffer
  SC_LOG_DEBUG(this, transaction, "Write transaction in Tx buffer");
  tx_buffer.push_back(transaction_copy);
  tx_buffer_in_event.notify(delay);

  // begin response to bus
  tlm_phase resp_phase = BEGIN_RESP;
  sc_time resp_delay = SC_ZERO_TIME;

  bus_target_socket->nb_transport_bw(transaction, resp_phase, resp_delay);

  phase = END_REQ;
  return TLM_COMPLETED;
}

tlm_sync_enum
Interconnect::nb_transport_fw_interconnect(tlm_generic_payload &transaction,
                                           tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "Received request from Interconnect");

  output_buffer_levels();

  auto *transaction_copy = static_cast<ChipletPayload *>(&transaction)->clone();

  if (rx_buffer.size() == INTERCONNECT_BUFFER_SIZE) {
    SC_LOG_WARN(this, transaction, "Rx buffer full -> waiting...");
    wait(rx_buffer_out_event);
  }

  // add interconnect transfer delay
  delay += get_interconnect_transfer_delay(*this, transaction);

  // put transaction in rx buffer
  SC_LOG_DEBUG(this, transaction, "Write transaction in Rx buffer");
  rx_buffer.push_back(transaction_copy);
  rx_buffer_in_event.notify(delay);

  // begin response to interconnect
  tlm_phase resp_phase = BEGIN_RESP;
  sc_time resp_delay = SC_ZERO_TIME;

  interconnect_target_socket->nb_transport_bw(transaction, resp_phase,
                                              resp_delay);

  phase = END_REQ;
  return TLM_COMPLETED;
}

tlm_sync_enum
Interconnect::nb_transport_bw_bus(tlm_generic_payload &transaction,
                                  tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction, "Received response from Bus");

    delay += get_bus_transfer_delay(*this, transaction);

    rx_transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum
Interconnect::nb_transport_bw_interconnect(tlm_generic_payload &transaction,
                                           tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction, "Received response from Interconnect");

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

// helper functions
void Interconnect::output_buffer_levels() {
  SC_LOG_DEBUG_NO_TX(this, "Buffer Levels");
  SC_LOG_DEBUG_NO_TX(this, "Tx buffer: " << tx_buffer.size());
  SC_LOG_DEBUG_NO_TX(this, "Rx buffer: " << rx_buffer.size());
}