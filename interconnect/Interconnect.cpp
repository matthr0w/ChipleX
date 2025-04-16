#include "Interconnect.h"
#include "chiplet/Delays.h"

#include <deque>

#include "include/logging.h"
#include "include/payload.h"

Interconnect::Interconnect(sc_module_name name)
    : sc_module(name), socket_A_in("socket_A_in"), socket_B_in("socket_B_in"),
      socket_A_out("socket_A_out"), socket_B_out("socket_B_out"),
      peq_A_to_B("peq_A_to_B"), peq_B_to_A("peq_B_to_A"), m_buffer_depth(3),
      m_width_bits(8), m_clk_cycle(sc_time(4, SC_NS)) {
  socket_A_in.register_nb_transport_fw(this, &Interconnect::nb_transport_fw_A);
  socket_B_in.register_nb_transport_fw(this, &Interconnect::nb_transport_fw_B);
  socket_A_out.register_nb_transport_bw(this, &Interconnect::nb_transport_bw_A);
  socket_B_out.register_nb_transport_bw(this, &Interconnect::nb_transport_bw_B);

  SC_THREAD(process_tx_buffer_A);
  SC_THREAD(process_rx_buffer_A);
  SC_THREAD(process_tx_buffer_B);
  SC_THREAD(process_rx_buffer_B);
}

void Interconnect::process_tx_buffer_A() {
  while (true) {
    wait(tx_buffer_A_event);

    while (!tx_buffer_A.empty()) {
      if (rx_buffer_B.size() == m_buffer_depth) {
        SC_LOG_WARN(this, "A->B: rx buffer B full -> waiting...");
        wait(transaction_B_done);
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

      SC_LOG_DEBUG(this, "A->RAM: transmission started");

      tlm_sync_enum tlm_resp =
          socket_A_out->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        // bus processes the request
        wait(delay);
      } else if (tlm_resp == TLM_ACCEPTED) {
        // bus accepted the request but is busy (bufferd)
      }

      wait(transaction_A_done);

      SC_LOG_DEBUG(this, "A->RAM: transmission finished");

      // remove from rx buffer A
      rx_buffer_A.pop_front();
      
      delete transaction;
    }
  }
}

void Interconnect::process_tx_buffer_B() {
  while (true) {
    wait(tx_buffer_B_event);

    while (!tx_buffer_B.empty()) {
      if (rx_buffer_A.size() == m_buffer_depth) {
        SC_LOG_WARN(this, "B->A: rx buffer A full -> waiting...");
        wait(transaction_A_done);
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

      SC_LOG_DEBUG(this, "B->RAM: transmission started");

      tlm_sync_enum tlm_resp =
          socket_B_out->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        // bus processes the request
        wait(delay);
      } else if (tlm_resp == TLM_ACCEPTED) {
        // bus accepted the request but is busy (bufferd)
      }

      wait(transaction_B_done);

      SC_LOG_DEBUG(this, "B->RAM: transmission finished");

      // remove from rx buffer B
      rx_buffer_B.pop_front();

      delete transaction;
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

  output_buffer_levels();

  auto *transaction_copy = static_cast<payload*>(&transaction)->clone();

  if (tx_buffer_A.size() == m_buffer_depth) {
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

  output_buffer_levels();

  auto *transaction_copy = static_cast<payload*>(&transaction)->clone();

  if (tx_buffer_B.size() == m_buffer_depth) {
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
tlm_generic_payload *
Interconnect::copy_transaction(tlm_generic_payload &transaction) {
  tlm_generic_payload *copy = new tlm_generic_payload;
  copy->deep_copy_from(transaction);

  if (transaction.get_data_ptr() && transaction.get_data_length() > 0) {
    unsigned char *copy_data = new unsigned char[transaction.get_data_length()];
    std::memcpy(copy_data, transaction.get_data_ptr(),
                transaction.get_data_length());
    copy->set_data_ptr(copy_data);
  }

  return copy;
}

void Interconnect::free_transaction(tlm_generic_payload *transaction) {
  if (transaction) {
    if (transaction->get_data_ptr()) {
      delete[] transaction->get_data_ptr();
    }

    delete transaction;
  }
}

sc_time Interconnect::compute_delay(tlm_generic_payload *transaction) {
  size_t size = transaction->get_data_length();
  size_t size_bits = size * 8;
  size_t cycles = (size_bits + m_width_bits - 1) / m_width_bits;
  return cycles * m_clk_cycle;
}

void Interconnect::output_buffer_levels() {
  SC_LOG_DEBUG(this, "Buffer Levels");
  SC_LOG_DEBUG(this, "tx buffer A: " << tx_buffer_A.size());
  SC_LOG_DEBUG(this, "rx buffer A: " << rx_buffer_A.size());
  SC_LOG_DEBUG(this, "tx buffer B: " << tx_buffer_B.size());
  SC_LOG_DEBUG(this, "rx buffer B: " << rx_buffer_B.size());
}