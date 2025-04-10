#include "Bus.h"
#include "Delays.h"

#include <deque>

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

Bus::Bus(sc_module_name name)
    : sc_module(name), target_socket_core1("target_socket_core1"),
      target_socket_core2("target_socket_core2"),
      initiator_socket("initiator_socket"), peq("peq"),
      current_owner(0) // 0: free, 1: Core1, 2: Core2
{
  target_socket_core1.register_nb_transport_fw(this, &Bus::nb_transport_fw, 1);
  target_socket_core2.register_nb_transport_fw(this, &Bus::nb_transport_fw, 2);
  initiator_socket.register_nb_transport_bw(this, &Bus::nb_transport_bw);

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void Bus::process_transaction() {
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq.get_next_transaction();

    int transaction_owner = current_owner;

    SC_LOG_DEBUG(this, "Processing transaction for Core" << transaction_owner);

    delay = SC_ZERO_TIME;

    // path is differentiated via command
    // forward path to RAM
    if (transaction->is_read() || transaction->is_write()) {
      phase = BEGIN_REQ;

      SC_LOG_DEBUG(this, "Forwarding BEGIN_REQ for Core" << transaction_owner
                                                         << " to RAM");

      tlm_resp = initiator_socket->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }
    }

    // backward path to core
    else {
      phase = BEGIN_RESP;

      SC_LOG_DEBUG(this, "Forwarding BEGIN_RESP to Core" << transaction_owner);

      if (transaction_owner == 1) {
        tlm_resp =
            target_socket_core1->nb_transport_bw(*transaction, phase, delay);
      } else {
        tlm_resp =
            target_socket_core2->nb_transport_bw(*transaction, phase, delay);
      }

      if (tlm_resp == TLM_COMPLETED) {
        wait(delay);

        // release bus
        current_owner = 0;

        // process queue
        if (!m_request_queue.empty()) {
          process_queue();
        }
      }
    }
  }
}

void Bus::process_queue() {
  tlm_generic_payload *next_transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  // dequeue next waiting request
  BusRequest next_req = m_request_queue.front();
  m_request_queue.pop_front();

  // grant access
  current_owner = next_req.core_id;
  next_transaction = next_req.transaction;

  delay = get_bus_arbitration_delay();

  SC_LOG_INFO(this,
              "Granting bus access to Core" << current_owner << " from queue");

  peq.notify(*next_transaction, delay);

  phase = END_REQ;
  delay = SC_ZERO_TIME;

  SC_LOG_DEBUG(this, "Sending END_REQ to Core" << current_owner);
  if (current_owner == 1) {
    tlm_resp =
        target_socket_core1->nb_transport_bw(*next_transaction, phase, delay);
  } else {
    tlm_resp =
        target_socket_core2->nb_transport_bw(*next_transaction, phase, delay);
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Bus::nb_transport_fw(int id, tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, "Received request from Core" << id);

  if (phase != BEGIN_REQ) {
    SC_LOG_ERROR(this, "Protocol Error: Request from Core" << id << " with "
                                                           << phase);
    exit(1);
  }

  if (current_owner == 0 && m_request_queue.empty()) {
    // bus is free and queue is empty: grant access immediately
    SC_LOG_INFO(this, "Bus is empty -> granting bus access to Core" << id);

    current_owner = id;

    delay = get_bus_arbitration_delay();

    peq.notify(transaction, delay);

    phase = END_REQ;
    return TLM_UPDATED;
  } else {
    // bus is busy or queue is not empty: enqueue request
    SC_LOG_INFO(this, "Bus is busy with Core"
                          << current_owner << " -> enqueuing request from Core"
                          << id);

    m_request_queue.push_back({id, &transaction});

    return TLM_ACCEPTED;
  }
}

tlm_sync_enum Bus::nb_transport_bw(tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, "Received response from RAM");

  if (phase == BEGIN_RESP) {
    delay += get_bus_arbitration_delay();

    peq.notify(transaction, delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  } else {
    return TLM_ACCEPTED;
  }
}