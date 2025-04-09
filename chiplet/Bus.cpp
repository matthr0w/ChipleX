#include "Bus.h"

#include <deque>

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

Bus::Bus(sc_module_name name)
    : sc_module(name), target_socket_core1("target_socket_core1"),
      target_socket_core2("target_socket_core2"),
      initiator_socket("initiator_socket"), peq("peq"),
      current_owner(0), // 0: free, 1: Core1, 2: Core2
      arbitration_delay(sc_time(5, SC_NS)) {
  target_socket_core1.register_nb_transport_fw(this, &Bus::nb_transport_fw, 1);
  target_socket_core2.register_nb_transport_fw(this, &Bus::nb_transport_fw, 2);
  initiator_socket.register_nb_transport_bw(this, &Bus::nb_transport_bw);

  SC_THREAD(process);
  sensitive << peq.get_event();
}

void Bus::grant() {
  tlm_generic_payload *next_payload;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  // Dequeue next waiting request
  BusRequest next_req = m_request_queue.front();
  m_request_queue.pop_front();

  // Grant access
  current_owner = next_req.core_id;
  next_payload = next_req.payload;
  delay = arbitration_delay; // DELAY

  SC_LOG_INFO(this,
              "Granting bus access to Core" << current_owner << " from queue");

  peq.notify(*next_payload, delay);

  phase = END_REQ;
  delay = SC_ZERO_TIME;

  SC_LOG_DEBUG(this, "Sending END_REQ to Core" << current_owner);
  if (current_owner == 1) {
    tlm_resp =
        target_socket_core1->nb_transport_bw(*next_payload, phase, delay);
  } else {
    tlm_resp =
        target_socket_core2->nb_transport_bw(*next_payload, phase, delay);
  }

  if (tlm_resp == TLM_ACCEPTED) {
    SC_LOG_DEBUG(this, "Core" << current_owner << " accepted END_REQ");
  } else {
    SC_LOG_ERROR(this, "Protocol Error: Core" << current_owner
                                              << " not accepted END_REQ");
    exit(1);
  }
}

void Bus::process() {
  tlm_generic_payload *payload;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();
    payload = peq.get_next_transaction();

    int transaction_owner = current_owner;

    SC_LOG_DEBUG(this, "Processing transaction from PEQ for Core"
                           << transaction_owner);

    delay = SC_ZERO_TIME;

    // Path is differentiated via command
    // Forward path (BEGIN_REQ to RAM)
    if (payload->is_read() || payload->is_write()) {
      phase = BEGIN_REQ;

      SC_LOG_DEBUG(this, "Forwarding BEGIN_REQ for Core" << transaction_owner
                                                         << " to RAM");

      tlm_resp = initiator_socket->nb_transport_fw(*payload, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }
    }

    // Backward path (BEGIN_RESP to Core)
    else {
      phase = BEGIN_RESP;

      SC_LOG_DEBUG(this, "Forwarding BEGIN_RESP to Core" << transaction_owner);

      if (transaction_owner == 1) {
        tlm_resp = target_socket_core1->nb_transport_bw(*payload, phase, delay);
      } else {
        tlm_resp = target_socket_core2->nb_transport_bw(*payload, phase, delay);
      }

      if (tlm_resp == TLM_COMPLETED) {
        wait(delay);

        // Release bus
        current_owner = 0;

        // Check queue
        if (!m_request_queue.empty()) {
          grant();
        }
      }
    }
  }
}

tlm_sync_enum Bus::nb_transport_fw(int id, tlm_generic_payload &payload,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, "Received request from Core" << id);

  if (phase != BEGIN_REQ) {
    SC_LOG_ERROR(this, "Protocol Error: Request from Core" << id << " with "
                                                           << phase);
    exit(1);
  }

  // Arbitration Logic: Queue-based FIFO
  if (current_owner == 0 && m_request_queue.empty()) {
    // Bus is free and queue is empty: grant access immediately
    current_owner = id;
    delay += arbitration_delay; // DELAY

    SC_LOG_INFO(this, "Granting bus access to Core" << id);

    peq.notify(payload, delay);

    phase = END_REQ;
    payload.set_response_status(TLM_OK_RESPONSE);

    return TLM_UPDATED;
  } else {
    // Bus is busy or queue is not empty: enqueue request
    SC_LOG_INFO(this, "Bus is busy (Owner: Core"
                          << current_owner << ") -> enqueuing request from Core"
                          << id);
    m_request_queue.push_back({id, &payload});

    payload.set_response_status(TLM_OK_RESPONSE);

    return TLM_ACCEPTED;
  }
}

tlm_sync_enum Bus::nb_transport_bw(tlm_generic_payload &payload,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, "Received response from RAM");

  if (phase == BEGIN_RESP) {
    delay += arbitration_delay; // DELAY

    peq.notify(payload, delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  } else {
    return TLM_ACCEPTED;
  }
}