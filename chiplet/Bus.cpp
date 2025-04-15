#include "Bus.h"
#include "Delays.h"

#include <deque>

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

Bus::Bus(sc_module_name name, unsigned int id)
    : sc_module(name), id(id), target_socket_core1("target_socket_core1"),
      target_socket_core2("target_socket_core2"),
      ram_initiator_socket("ram_initiator_socket"),
      interconnect_initiator_socket("interconnect_initiator_socket"),
      peq("peq"), current_owner(0) // 0: free, 1: Core1, 2: Core2, 3: Interface
{
  target_socket_core1.register_nb_transport_fw(this, &Bus::nb_transport_fw, 1);
  target_socket_core2.register_nb_transport_fw(this, &Bus::nb_transport_fw, 2);
  interconnect_target_socket.register_nb_transport_fw(this,
                                                      &Bus::nb_transport_fw, 3);
  ram_initiator_socket.register_nb_transport_bw(this, &Bus::nb_transport_bw, 4);

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
    uint32_t address = transaction->get_address();
    int destination = address >> 16;

    SC_LOG_DEBUG(this, "Processing transaction for Module" << current_owner);

    delay = SC_ZERO_TIME;

    if (destination == 1) {
      phase = BEGIN_REQ;

      SC_LOG_DEBUG(this, "Forwarding BEGIN_REQ for Core" << current_owner
                                                         << " to Interconnect");

      tlm_resp = interconnect_initiator_socket->nb_transport_fw(*transaction,
                                                                phase, delay);

      if (tlm_resp == TLM_COMPLETED) {
        wait(delay);
      }

      phase = END_REQ;
      delay = SC_ZERO_TIME;

      if (current_owner == 1) {
        tlm_resp =
            target_socket_core1->nb_transport_bw(*transaction, phase, delay);
      } else if (current_owner == 2) {
        tlm_resp =
            target_socket_core2->nb_transport_bw(*transaction, phase, delay);
      }

      // release bus
      current_owner = 0;

      // process queue
      if (!m_request_queue.empty()) {
        process_queue();
      }
    } else {
      // path is differentiated via command
      // forward path to RAM
      if (transaction->is_read() || transaction->is_write()) {
        phase = BEGIN_REQ;

        SC_LOG_DEBUG(this, "Forwarding BEGIN_REQ for Core" << current_owner
                                                           << " to RAM");

        tlm_resp =
            ram_initiator_socket->nb_transport_fw(*transaction, phase, delay);

        if (tlm_resp == TLM_UPDATED) {
          wait(delay);
        }
      }

      // backward path
      else {
        phase = BEGIN_RESP;

        SC_LOG_DEBUG(this, "Forwarding BEGIN_RESP to Core" << current_owner);

        if (current_owner == 1) {
          tlm_resp =
              target_socket_core1->nb_transport_bw(*transaction, phase, delay);
        } else if (current_owner == 2) {
          tlm_resp =
              target_socket_core2->nb_transport_bw(*transaction, phase, delay);
        } else if (current_owner == 3) {
          // tlm_resp =
          // interconnect_target_socket->nb_transport_bw(*transaction, phase,
          // delay);
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
  current_owner = next_req.module;
  next_transaction = next_req.transaction;

  delay = get_bus_arbitration_delay();

  SC_LOG_INFO(this, "Granting bus access to Module" << current_owner
                                                    << " from queue");

  peq.notify(*next_transaction, delay);

  phase = END_REQ;
  delay = SC_ZERO_TIME;

  // end request
  if (current_owner == 1) {
    tlm_resp =
        target_socket_core1->nb_transport_bw(*next_transaction, phase, delay);
  } else if (current_owner == 2) {
    tlm_resp =
        target_socket_core2->nb_transport_bw(*next_transaction, phase, delay);
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Bus::nb_transport_fw(int id, tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, "Received request from Module" << id);

  if (phase != BEGIN_REQ) {
    SC_LOG_ERROR(this, "Protocol Error: Request from Module" << id << " with "
                                                             << phase);
    exit(1);
  }

  if (current_owner == 0 && m_request_queue.empty()) {
    // bus is free and queue is empty: grant access immediately
    SC_LOG_INFO(this, "Bus is empty -> granting bus access to Module" << id);

    current_owner = id;

    delay = get_bus_arbitration_delay();

    peq.notify(transaction, delay);

    phase = END_REQ;
    return TLM_UPDATED;
  } else {
    // bus is busy or queue is not empty: enqueue request
    SC_LOG_INFO(this, "Bus is busy with Module"
                          << current_owner
                          << " -> enqueuing request from Module" << id);

    uint32_t address = transaction.get_address();
    m_request_queue.push_back({id, &transaction});

    return TLM_ACCEPTED;
  }
}

tlm_sync_enum Bus::nb_transport_bw(int id, tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, "Received response from Module" << id);

  if (phase != BEGIN_RESP) {
    SC_LOG_ERROR(this, "Protocol Error: Response from Module" << id << " with "
                                                              << phase);
    exit(1);
  }

  delay += get_bus_arbitration_delay();

  peq.notify(transaction, delay);

  phase = END_RESP;
  return TLM_COMPLETED;
}