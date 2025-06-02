#include "Bus.h"

#include "common/Delays.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

fpga::Bus::Bus(sc_module_name name, unsigned int id)
    : sc_module(name), utilization_tracker(this->name()), fpga_id(id),
      generator_target_socket("generator_target_socket"),
      interconnect_target_socket("interconnect_target_socket"),
      interconnect_initiator_socket("interconnect_initiator_socket"),
      ram_initiator_socket("ram_initiator_socket"), peq_fw("peq_fw"),
      peq_bw("peq_bw"), current_owner(0) {
  generator_target_socket.register_nb_transport_fw(
      this, &fpga::Bus::nb_transport_fw, 1);

  interconnect_target_socket.register_nb_transport_fw(
      this, &fpga::Bus::nb_transport_fw, 2);
  interconnect_initiator_socket.register_nb_transport_bw(
      this, &fpga::Bus::nb_transport_bw, 2);

  ram_initiator_socket.register_nb_transport_bw(this,
                                                &fpga::Bus::nb_transport_bw, 3);

  SC_THREAD(process_transaction_fw);
  sensitive << peq_fw.get_event();
  SC_THREAD(process_transaction_bw);
  sensitive << peq_bw.get_event();
}

void fpga::Bus::process_transaction_fw() {
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

    if (ext->destination_id != fpga_id) {
      SC_LOG_DEBUG(this, *transaction,
                   "PROTOCOL: Forwarding BEGIN_REQ for "
                       << modules[current_owner] << " to Interconnect");

      tlm_resp = interconnect_initiator_socket->nb_transport_fw(*transaction,
                                                                phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }
    } else {
      SC_LOG_DEBUG(this, *transaction,
                   "PROTOCOL: Forwarding BEGIN_REQ for "
                       << modules[current_owner] << " to RAM");

      tlm_resp =
          ram_initiator_socket->nb_transport_fw(*transaction, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }
    }
  }
}

void fpga::Bus::process_transaction_bw() {
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
                 "PROTOCOL: Backwarding BEGIN_RESP to "
                     << modules[current_owner]);

    // begin response
    if (current_owner == 1) {
      tlm_resp =
          generator_target_socket->nb_transport_bw(*transaction, phase, delay);
    } else if (current_owner == 2) {
      tlm_resp = interconnect_target_socket->nb_transport_bw(*transaction,
                                                             phase, delay);
    }

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);

      // release bus
      current_owner = 0;

      utilization_tracker.set_idle();

      // process queue
      if (!request_queue.empty()) {
        process_queue();
      }
    }
  }
}

void fpga::Bus::process_queue() {
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
               "Granting bus access to " << modules[current_owner]
                                         << " from queue");

  peq_fw.notify(*next_transaction, delay);

  phase = END_REQ;
  delay = SC_ZERO_TIME;

  // end request
  if (current_owner == 1) {
    tlm_resp = generator_target_socket->nb_transport_bw(*next_transaction,
                                                        phase, delay);
  } else if (current_owner == 2) {
    tlm_resp = interconnect_target_socket->nb_transport_bw(*next_transaction,
                                                           phase, delay);
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum fpga::Bus::nb_transport_fw(int id,
                                         tlm_generic_payload &transaction,
                                         tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "PROTOCOL: Received request from " << modules[id]);

  if (phase != BEGIN_REQ) {
    SC_LOG_ERROR(this, transaction,
                 "PROTOCOL ERROR: Request from " << modules[id] << " with "
                                                 << phase);
    exit(1);
  }

  utilization_tracker.set_active();

  if (current_owner == 0 && request_queue.empty()) {
    // bus is free and queue is empty: grant access immediately
    SC_LOG_DEBUG(this, transaction,
                 "Bus is empty -> granting bus access to " << modules[id]);

    current_owner = id;

    delay =
        get_bus_arbitration_delay(*this, transaction, bus_arbitration_delay);

    peq_fw.notify(transaction, delay);

    phase = END_REQ;
    return TLM_UPDATED;
  } else {
    // bus is busy or queue is not empty: enqueue request
    SC_LOG_DEBUG(this, transaction,
                 "Bus is busy with " << modules[current_owner]
                                     << " -> enqueuing request from "
                                     << modules[id]);

    if (id == 2) { // to avoid deadlocks prefer interconnect
      request_queue.push_front({id, &transaction});
    } else {
      request_queue.push_back({id, &transaction});
    }

    return TLM_ACCEPTED;
  }
}

tlm_sync_enum fpga::Bus::nb_transport_bw(int id,
                                         tlm_generic_payload &transaction,
                                         tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "PROTOCOL: Received response from " << modules[id]);

  if (phase != BEGIN_RESP) {
    SC_LOG_ERROR(this, transaction,
                 "PROTOCOL ERROR: Response from " << modules[id] << " with "
                                                  << phase);
    exit(1);
  }

  delay += get_bus_arbitration_delay(*this, transaction, bus_arbitration_delay);

  peq_bw.notify(transaction, delay);

  phase = END_RESP;
  return TLM_COMPLETED;
}