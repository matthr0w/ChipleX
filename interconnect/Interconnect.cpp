#include "Interconnect.h"
#include "chiplet/Delays.h"

#include <deque>

#include "include/logging.h"

Interconnect::Interconnect(sc_module_name name)
    : sc_module(name), socket_A_in("socket_A_in"), socket_B_in("socket_B_in"),
      socket_A_out("socket_A_out"), socket_B_out("socket_B_out"),
      peq_A_to_B("peq_A_to_B"), peq_B_to_A("peq_B_to_A"), m_buffer_depth(1),
      m_width_bits(8), m_clk_cycle(sc_time(4, SC_NS)) {
  socket_A_in.register_nb_transport_fw(this, &Interconnect::nb_transport_fw_A);
  socket_B_in.register_nb_transport_fw(this, &Interconnect::nb_transport_fw_B);
  socket_A_out.register_nb_transport_bw(this, &Interconnect::nb_transport_bw_A);
  socket_B_out.register_nb_transport_bw(this, &Interconnect::nb_transport_bw_B);

  ready_A.write(true);
  ready_B.write(true);
  rx_buffer_A_full.write(false);
  rx_buffer_B_full.write(false);

  SC_THREAD(process_tx_buffer_A);
  SC_THREAD(process_rx_buffer_A);
  SC_THREAD(process_tx_buffer_B);
  SC_THREAD(process_rx_buffer_B);
}

void Interconnect::process_tx_buffer_A() {
  while (true) {
    wait(tx_buffer_A_event);

    while (!tx_buffer_A.empty()) {
      ready_A.write(!(tx_buffer_A.size() == m_buffer_depth));

      if (rx_buffer_B_full.read()) {
        SC_LOG_WARN(this, "A->B: rx buffer B full -> waiting...");
        wait(rx_buffer_B_full.negedge_event());
        continue;
      }

      tlm_generic_payload *transaction = tx_buffer_A.front();

      SC_LOG_DEBUG(this, "A->B: transmission started");
      sc_time delay = compute_delay(transaction);
      wait(delay);
      SC_LOG_DEBUG(this, "A->B: transmission finished");

      // remove from tx buffer A and put in rx buffer B
      tx_buffer_A.pop_front();
      rx_buffer_B.push_back(transaction);
      rx_buffer_B_event.notify();

      ready_A.write(!(tx_buffer_A.size() == m_buffer_depth));
    }
  }
}

void Interconnect::process_rx_buffer_A() {
  while (true) {
    wait(rx_buffer_A_event);

    while (!rx_buffer_A.empty()) {
      tlm_generic_payload *transaction = rx_buffer_A.front();
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;

      SC_LOG_DEBUG(this, "A->Bus: transmission started");

      tlm_sync_enum tlm_resp =
          socket_A_out->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        // bus processes the request
        wait(delay);
      } else if (tlm_resp == TLM_ACCEPTED) {
        // bus accepted the request but is busy (bufferd)
      }

      wait(transaction_A_done);

      SC_LOG_DEBUG(this, "A->Bus: transmission finished");

      // remove from rx buffer A
      rx_buffer_A.pop_front();
      rx_buffer_A_event.notify();
      rx_buffer_A_full.write(rx_buffer_A.size() == m_buffer_depth);
      free_transaction(transaction);

      SC_LOG_DEBUG(this, "rx buffer A full? " << rx_buffer_A_full.read());
    }
  }
}

void Interconnect::process_tx_buffer_B() {
  while (true) {
    wait(tx_buffer_B_event);

    while (!tx_buffer_B.empty()) {
      ready_B.write(!(tx_buffer_B.size() == m_buffer_depth));

      if (rx_buffer_A_full.read()) {
        SC_LOG_WARN(this, "B->A: rx buffer A full -> waiting...");
        wait(rx_buffer_A_full.negedge_event());
        continue;
      }

      tlm_generic_payload *transaction = tx_buffer_B.front();

      SC_LOG_DEBUG(this, "B->A: transmission started");
      sc_time delay = compute_delay(transaction);
      wait(delay);
      SC_LOG_DEBUG(this, "B->A: transmission finished");

      // remove from tx buffer B and put in rx buffer A
      tx_buffer_B.pop_front();
      rx_buffer_A.push_back(transaction);
      rx_buffer_A_event.notify();

      ready_B.write(!(tx_buffer_B.size() == m_buffer_depth));
    }
  }
}

void Interconnect::process_rx_buffer_B() {
  while (true) {
    wait(rx_buffer_B_event);

    while (!rx_buffer_B.empty()) {
      tlm_generic_payload *transaction = rx_buffer_B.front();
      tlm_phase phase = BEGIN_REQ;
      sc_time delay = SC_ZERO_TIME;

      SC_LOG_DEBUG(this, "B->Bus: transmission started");

      tlm_sync_enum tlm_resp =
          socket_B_out->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        // bus processes the request
        wait(delay);
      } else if (tlm_resp == TLM_ACCEPTED) {
        // bus accepted the request but is busy (bufferd)
      }

      wait(transaction_B_done);

      SC_LOG_DEBUG(this, "B->Bus: transmission finished");

      // remove from rx buffer B
      rx_buffer_B.pop_front();
      rx_buffer_B_event.notify();
      rx_buffer_B_full.write(rx_buffer_B.size() == m_buffer_depth);
      free_transaction(transaction);

      SC_LOG_DEBUG(this, "rx buffer B full? " << rx_buffer_B_full.read());
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Interconnect::nb_transport_fw_A(tlm_generic_payload &transaction,
                                              tlm_phase &phase,
                                              sc_time &delay) {
  SC_LOG_DEBUG(this, "A->B: request received");

  tlm_generic_payload *transaction_copy = copy_transaction(transaction);

  if (!ready_A.read()) {
    SC_LOG_WARN(this, "A->B: A not ready -> waiting...");
    wait(rx_buffer_B_event);
  }

  // add bus transfer delay
  delay += get_bus_transfer_delay(transaction_copy->get_data_length());

  // put transaction in tx buffer A
  SC_LOG_DEBUG(this, "A->B: put transaction in tx buffer A");
  tx_buffer_A.push_back(transaction_copy);
  tx_buffer_A_event.notify(delay);

  phase = END_REQ;
  return TLM_COMPLETED;
}

tlm_sync_enum Interconnect::nb_transport_fw_B(tlm_generic_payload &transaction,
                                              tlm_phase &phase,
                                              sc_time &delay) {
  SC_LOG_DEBUG(this, "B->A: request received");

  tlm_generic_payload *transaction_copy = copy_transaction(transaction);

  if (!ready_B.read()) {
    SC_LOG_WARN(this, "B->A: B not ready -> waiting...");
    wait(rx_buffer_A_event);
  }

  // add bus transfer delay
  delay += get_bus_transfer_delay(transaction_copy->get_data_length());

  // put transaction in tx buffer B
  SC_LOG_DEBUG(this, "Put transaction in tx buffer B");
  tx_buffer_B.push_back(transaction_copy);
  tx_buffer_B_event.notify(delay);

  phase = END_REQ;
  return TLM_COMPLETED;
}

tlm_sync_enum Interconnect::nb_transport_bw_A(tlm_generic_payload &transaction,
                                              tlm_phase &phase,
                                              sc_time &delay) {
  if (phase == BEGIN_RESP) {
    delay += get_bus_transfer_delay(transaction.get_data_length());

    transaction_A_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum Interconnect::nb_transport_bw_B(tlm_generic_payload &transaction,
                                              tlm_phase &phase,
                                              sc_time &delay) {
  if (phase == BEGIN_RESP) {
    delay += get_bus_transfer_delay(transaction.get_data_length());

    transaction_B_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

// helper functions
sc_time Interconnect::compute_delay(tlm_generic_payload *transaction) {
  size_t size = transaction->get_data_length();
  size_t size_bits = size * 8;
  size_t cycles = (size_bits + m_width_bits - 1) / m_width_bits;
  return cycles * m_clk_cycle;
}