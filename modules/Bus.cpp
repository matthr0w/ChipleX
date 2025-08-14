#include "Bus.h"

#include "common/Delays.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

Bus::Bus(sc_module_name name, unsigned int chip_id, unsigned int num_managers,
         unsigned int num_subordinates, sc_time bus_arbitration_delay)
    : sc_module(name), chip_id(chip_id),
      bus_arbitration_delay(bus_arbitration_delay),
      utilization_tracker(this->name()), peq_fw("peq_fw"), peq_bw("peq_bw"),
      current_owner(-1) {
  manager_target_sockets = new simple_target_socket_tagged<Bus>[num_managers];
  subordinate_initiator_sockets = new simple_initiator_socket_tagged<Bus>[num_subordinates];

  for (unsigned int i = 0; i < num_managers; ++i) {
    manager_target_sockets[i].register_nb_transport_fw(this,
                                                      &Bus::nb_transport_fw, i);
  }

  for (unsigned int i = 0; i < num_subordinates; ++i) {
    subordinate_initiator_sockets[i].register_nb_transport_bw(
        this, &Bus::nb_transport_bw, i);
  }

  SC_THREAD(process_transaction_fw);
  sensitive << peq_fw.get_event();
  SC_THREAD(process_transaction_bw);
  sensitive << peq_bw.get_event();
}

Bus::~Bus() {
  delete[] manager_target_sockets;
  delete[] subordinate_initiator_sockets;
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

    phase = BEGIN_REQ;
    delay = SC_ZERO_TIME;

    if (ext->destination_id != chip_id) {
      SC_LOG_DEBUG(this, *transaction,
                   "PROTOCOL: Forwarding BEGIN_REQ for " << current_owner
                                                         << " to Interconnect");

      tlm_resp = subordinate_initiator_sockets[0]->nb_transport_fw(*transaction,
                                                             phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }
    } else {
      SC_LOG_DEBUG(this, *transaction,
                   "PROTOCOL: Forwarding BEGIN_REQ for " << current_owner
                                                         << " to RAM");

      tlm_resp = subordinate_initiator_sockets[1]->nb_transport_fw(*transaction,
                                                             phase, delay);

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
                 "PROTOCOL: Backwarding BEGIN_RESP to " << current_owner);

    // begin response
    tlm_resp = manager_target_sockets[current_owner]->nb_transport_bw(
        *transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);

      // release bus
      current_owner = -1;

      utilization_tracker.set_idle();

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

  delay = get_bus_arbitration_delay(*this, *next_transaction,
                                    bus_arbitration_delay);

  SC_LOG_DEBUG(this, *next_transaction,
               "Granting Bus access to " << current_owner << " from queue");

  peq_fw.notify(*next_transaction, delay);

  phase = END_REQ;
  delay = SC_ZERO_TIME;

  // end request
  tlm_resp = manager_target_sockets[current_owner]->nb_transport_bw(
      *next_transaction, phase, delay);
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Bus::nb_transport_fw(int id, tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received request from " << id);

  if (phase != BEGIN_REQ) {
    SC_LOG_ERROR(this, transaction,
                 "PROTOCOL ERROR: Request from " << id << " with " << phase);
    exit(1);
  }

  utilization_tracker.set_active();

  // set core id
  ChipletExtension *ext;
  transaction.get_extension(ext);
  if (ext->core_id == -1) {
    static_cast<ChipletPayload *>(&transaction)->set_core_id(id);
  }

  if (current_owner == -1 && request_queue.empty()) {
    // bus is free and queue is empty: grant access immediately
    SC_LOG_DEBUG(this, transaction,
                 "Bus is empty -> granting Bus access to " << id);

    current_owner = id;

    delay =
        get_bus_arbitration_delay(*this, transaction, bus_arbitration_delay);

    peq_fw.notify(transaction, delay);

    phase = END_REQ;
    return TLM_UPDATED;
  } else {
    // bus is busy or queue is not empty: enqueue request
    SC_LOG_DEBUG(this, transaction,
                 "Bus is busy with " << current_owner
                                     << " -> enqueuing request from " << id);

    if (id == 3) { // to avoid deadlocks prefer interconnect
      request_queue.push_front({id, &transaction});
    } else {
      request_queue.push_back({id, &transaction});
    }

    return TLM_ACCEPTED;
  }
}

tlm_sync_enum Bus::nb_transport_bw(int id, tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received response from " << id);

  if (phase != BEGIN_RESP) {
    SC_LOG_ERROR(this, transaction,
                 "PROTOCOL ERROR: Response from " << id << " with " << phase);
    exit(1);
  }

  delay += get_bus_arbitration_delay(*this, transaction, bus_arbitration_delay);

  peq_bw.notify(transaction, delay);

  phase = END_RESP;
  return TLM_COMPLETED;
}