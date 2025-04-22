#include "Bus.h"
#include "Delays.h"

#include <deque>

#include "common/RoutingTable.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

Bus::Bus(sc_module_name name, unsigned int id)
    : sc_module(name), chiplet_id(id),
      core0_target_socket("core0_target_socket"),
      core1_target_socket("core1_target_socket"),
      interconnect0_target_socket("interconnect0_target_socket"),
      interconnect1_target_socket("interconnect1_target_socket"),
      interconnect0_initiator_socket("interconnect0_initiator_socket"),
      interconnect1_initiator_socket("interconnect1_initiator_socket"),
      ram_initiator_socket("ram_initiator_socket"), peq_fw("peq_fw"),
      peq_bw("peq_bw"), current_owner(0) {
  core0_target_socket.register_nb_transport_fw(this, &Bus::nb_transport_fw, 1);
  core1_target_socket.register_nb_transport_fw(this, &Bus::nb_transport_fw, 2);

  interconnect0_target_socket.register_nb_transport_fw(
      this, &Bus::nb_transport_fw, 3);
  interconnect1_target_socket.register_nb_transport_fw(
      this, &Bus::nb_transport_fw, 4);

  interconnect0_initiator_socket.register_nb_transport_bw(
      this, &Bus::nb_transport_bw, 3);
  interconnect1_initiator_socket.register_nb_transport_bw(
      this, &Bus::nb_transport_bw, 4);

  ram_initiator_socket.register_nb_transport_bw(this, &Bus::nb_transport_bw, 5);

  SC_THREAD(process_transaction_fw);
  sensitive << peq_fw.get_event();
  SC_THREAD(process_transaction_bw);
  sensitive << peq_bw.get_event();
}

void Bus::process_transaction_fw() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq_fw.get_next_transaction();
    transaction->get_extension(ext);

    SC_LOG_DEBUG(this, *transaction,
                 "Processing forward transaction for "
                     << modules[current_owner]);

    phase = BEGIN_REQ;
    delay = SC_ZERO_TIME;

    if (ext->destination_id != chiplet_id) {
      int interconnect =
          RoutingTable::get_route(chiplet_id, ext->destination_id);

      if (interconnect == 0) {
        SC_LOG_DEBUG(this, *transaction,
                     "Forwarding BEGIN_REQ for " << modules[current_owner]
                                                 << " to Interconnect0");
        tlm_resp = interconnect0_initiator_socket->nb_transport_fw(
            *transaction, phase, delay);
      } else {
        SC_LOG_DEBUG(this, *transaction,
                     "Forwarding BEGIN_REQ for " << modules[current_owner]
                                                 << " to Interconnect1");
        tlm_resp = interconnect1_initiator_socket->nb_transport_fw(
            *transaction, phase, delay);
      }

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }
    } else {
      SC_LOG_DEBUG(this, *transaction,
                   "Forwarding BEGIN_REQ for " << modules[current_owner]
                                               << " to RAM");

      // if read operation, source becomes destination now
      if (transaction->get_command() == TLM_READ_COMMAND) {
        int source_id = ext->source_id;
        static_cast<ChipletPayload *>(transaction)
            ->set_destination_id(source_id);
      }

      tlm_resp =
          ram_initiator_socket->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }
    }
  }
}

void Bus::process_transaction_bw() {
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq_bw.get_next_transaction();

    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    SC_LOG_DEBUG(this, *transaction,
                 "Backwarding BEGIN_RESP to " << modules[current_owner]);

    // begin response
    if (current_owner == 1) {
      tlm_resp =
          core0_target_socket->nb_transport_bw(*transaction, phase, delay);
    } else if (current_owner == 2) {
      tlm_resp =
          core1_target_socket->nb_transport_bw(*transaction, phase, delay);
    } else if (current_owner == 3) {
      tlm_resp = interconnect0_target_socket->nb_transport_bw(*transaction,
                                                              phase, delay);
    } else if (current_owner == 4) {
      tlm_resp = interconnect1_target_socket->nb_transport_bw(*transaction,
                                                              phase, delay);
    }

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);

      // release bus
      current_owner = 0;

      // process queue
      if (!request_queue.empty()) {
        process_queue();
      }
    }
  }
}

void Bus::process_queue() {
  tlm_generic_payload *next_transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  // dequeue next waiting request
  BusRequest next_request = request_queue.front();
  request_queue.pop_front();

  // grant access
  current_owner = next_request.module;
  next_transaction = next_request.transaction;
  next_transaction->get_extension(ext);

  delay = get_bus_arbitration_delay(*this, *next_transaction);

  SC_LOG_INFO(this, *next_transaction,
              "Granting bus access to " << modules[current_owner]
                                        << " from queue");

  peq_fw.notify(*next_transaction, delay);

  phase = END_REQ;
  delay = SC_ZERO_TIME;

  // end request
  if (current_owner == 1) {
    tlm_resp =
        core0_target_socket->nb_transport_bw(*next_transaction, phase, delay);
  } else if (current_owner == 2) {
    tlm_resp =
        core1_target_socket->nb_transport_bw(*next_transaction, phase, delay);
  } else if (current_owner == 3) {
    tlm_resp = interconnect0_target_socket->nb_transport_bw(*next_transaction,
                                                            phase, delay);
  } else if (current_owner == 4) {
    tlm_resp = interconnect1_target_socket->nb_transport_bw(*next_transaction,
                                                            phase, delay);
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Bus::nb_transport_fw(int id, tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "Received request from " << modules[id]);

  if (phase != BEGIN_REQ) {
    SC_LOG_ERROR(this, transaction,
                 "Protocol Error: Request from " << modules[id] << " with "
                                                 << phase);
    exit(1);
  }

  if (current_owner == 0 && request_queue.empty()) {
    // bus is free and queue is empty: grant access immediately
    SC_LOG_INFO(this, transaction,
                "Bus is empty -> granting bus access to " << modules[id]);

    current_owner = id;

    delay = get_bus_arbitration_delay(*this, transaction);

    peq_fw.notify(transaction, delay);

    phase = END_REQ;
    return TLM_UPDATED;
  } else {
    // bus is busy or queue is not empty: enqueue request
    SC_LOG_INFO(this, transaction,
                "Bus is busy with " << modules[current_owner]
                                    << " -> enqueuing request from "
                                    << modules[id]);

    if (id == 3 || id == 4) { // to avoid deadlocks prefer interconnects
      request_queue.push_front({id, &transaction});
    } else {
      request_queue.push_back({id, &transaction});
    }

    return TLM_ACCEPTED;
  }
}

tlm_sync_enum Bus::nb_transport_bw(int id, tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "Received response from " << modules[id]);

  if (phase != BEGIN_RESP) {
    SC_LOG_ERROR(this, transaction,
                 "Protocol Error: Response from " << modules[id] << " with "
                                                  << phase);
    exit(1);
  }

  delay += get_bus_arbitration_delay(*this, transaction);

  peq_bw.notify(transaction, delay);

  phase = END_RESP;
  return TLM_COMPLETED;
}