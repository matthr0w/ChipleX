#include "InterconnectProtocol.h"

#include "common/Delays.h"
#include "common/Flits.h"
#include "common/RoutingTable.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

InterconnectProtocol::InterconnectProtocol(
    sc_module_name name, unsigned int chip_id, unsigned int num_cores,
    unsigned int num_interconnects, unsigned int flit_size,
    unsigned int overhead_size, sc_time pre_delay, sc_time post_delay,
    sc_time irq_delay, unsigned int bus_width, sc_time bus_clk_cycle)
    : sc_module(name), chip_id(chip_id), flit_size(flit_size),
      overhead_size(overhead_size), pre_delay(pre_delay),
      post_delay(post_delay), irq_delay(irq_delay), bus_width(bus_width),
      bus_clk_cycle(bus_clk_cycle), utilization_tracker(this->name()),
      bus_target_socket("bus_target_socket"),
      bus_initiator_socket("bus_initiator_socket"), peq_bus("peq_bus"),
      peq_phy("peq_phy"), current_interconnect(-1) {
  bus_target_socket.register_nb_transport_fw(
      this, &InterconnectProtocol::nb_transport_fw_bus);
  bus_initiator_socket.register_nb_transport_bw(
      this, &InterconnectProtocol::nb_transport_bw_bus);

  interconnect_target_sockets =
      new simple_target_socket_tagged<InterconnectProtocol>[num_interconnects];
  interconnect_initiator_sockets = new simple_initiator_socket_tagged<
      InterconnectProtocol>[num_interconnects];

  for (unsigned int i = 0; i < num_interconnects; ++i) {
    interconnect_target_sockets[i].register_nb_transport_fw(
        this, &InterconnectProtocol::nb_transport_fw_interconnect, i);
    interconnect_initiator_sockets[i].register_nb_transport_bw(
        this, &InterconnectProtocol::nb_transport_bw_interconnect, i);
  }

  irq_initiator_sockets =
      new simple_initiator_socket_tagged<InterconnectProtocol>[num_cores];

  SC_THREAD(process_bus_transaction);
  sensitive << peq_bus.get_event();
  SC_THREAD(process_phy_transaction);
  sensitive << peq_phy.get_event();
}

InterconnectProtocol::~InterconnectProtocol() {
  delete[] interconnect_target_sockets;
  delete[] interconnect_initiator_sockets;
}

void InterconnectProtocol::process_bus_transaction() {
  ChipletExtension *ext;
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    utilization_tracker.set_active();

    transaction = peq_bus.get_next_transaction();
    transaction->get_extension(ext);

    // set source id
    if (ext->source_id == -1) {
      static_cast<ChipletPayload *>(transaction)->set_source_id(chip_id);
    }

    // send flits
    send_flits(*transaction);

    // begin response to bus
    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp = bus_target_socket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }

    utilization_tracker.set_idle();
  }
}

void InterconnectProtocol::process_phy_transaction() {
  ChipletExtension *ext;
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq_phy.get_next_transaction();
    transaction->get_extension(ext);

    bool at_source = ext->source_id == chip_id;
    bool at_destination = ext->destination_id == chip_id;

    bool read_op = transaction->get_command() == TLM_READ_COMMAND;
    bool write_op = transaction->get_command() == TLM_WRITE_COMMAND;

    int flit_count = ext->flit_count;
    int flit_id = ext->flit_id;

    // at source and read operation:
    //    transaction was an off-chip read request
    //    -> set to write operation, send to RAM via bus
    //    -> send IRQ to core
    // at destination and read operation:
    //    transaction is an off-chip read request
    //    -> send to RAM via bus
    //    -> send back to source via interconnects
    // at destination and write operation:
    //    transaction is an off-chip write request
    //    -> send to RAM via bus
    //    -> send IRQ to core
    // not at source or destination
    //    transaction is not at the destination
    //    -> send to destination via interconnects

    if (at_source && read_op) {
      transaction->set_command(TLM_WRITE_COMMAND);
      send_to_bus(*transaction);
      if (flit_id == flit_count - 1) { // send IRQ on last flit
        send_irq(*transaction, TLM_READ_COMMAND);
      }
    } else if (at_destination) {
      if (read_op) {
        send_to_bus(*transaction);
        send_to_phy(*transaction);
      } else if (write_op) {
        send_to_bus(*transaction);
        if (flit_id == flit_count - 1) { // send IRQ on last flit
          send_irq(*transaction, TLM_WRITE_COMMAND);
        }
      }
    } else {
      send_to_phy(*transaction);
    }

    // begin response to interconnect
    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp =
        interconnect_target_sockets[current_interconnect]->nb_transport_bw(
            *transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);

      // release protocol layer
      current_interconnect = -1;

      utilization_tracker.set_idle();

      // process queue
      if (!request_queue.empty()) {
        process_queue();
      }
    }

    utilization_tracker.set_idle();
  }
}

// -------------------------------------------------------
// sender functions
// -------------------------------------------------------
void InterconnectProtocol::send_flits(tlm_generic_payload &transaction) {
  uint32_t address = transaction.get_address();
  unsigned char *data_ptr = transaction.get_data_ptr();
  unsigned int data_size = transaction.get_data_length();

  unsigned int flit_data_size = get_available_data_bytes_per_flit(transaction);
  unsigned int flit_count = get_required_flit_count(transaction);
  unsigned int flit_id = 0;

  unsigned int offset = 0;
  while (offset < data_size) {
    unsigned int current_data_size =
        std::min(flit_data_size, data_size - offset);

    auto *flit = static_cast<ChipletPayload &>(transaction).clone_ext();

    unsigned char *flit_data = new unsigned char[flit_data_size]();

    std::memcpy(flit_data, data_ptr + offset, current_data_size);

    flit->set_command(transaction.get_command());
    flit->set_address(address + offset);
    flit->set_data_ptr(flit_data);
    flit->set_data_length(current_data_size);
    flit->set_flit_count(flit_count);
    flit->set_flit_id(flit_id);
    flit->set_flit_padding(flit_data_size - current_data_size);

    send_to_phy(*flit);

    delete flit;

    flit_id += 1;
    offset += current_data_size;
  }
}

void InterconnectProtocol::send_to_phy(tlm_generic_payload &transaction) {
  ChipletExtension *ext;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  transaction.get_extension(ext);

  int route = RoutingTable::get_route(chip_id, ext->destination_id);

  if (route != -1) {
    SC_LOG_DEBUG(this, transaction,
                 "ROUTING: Chip ID " << chip_id << " Destination "
                                     << ext->destination_id
                                     << " Route to Interconnect" << route);

    tlm_resp = interconnect_initiator_sockets[route]->nb_transport_fw(
        transaction, phase, delay);

    if (tlm_resp == TLM_UPDATED) {
      wait(delay);
    }

    wait(phy_transaction_done);
  } else {
    SC_LOG_ERROR(this, transaction,
                 "ROUTING: Destination " << ext->destination_id
                                         << " not available.");
  }
}

void InterconnectProtocol::send_to_bus(tlm_generic_payload &transaction) {
  ChipletExtension *ext;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  tlm_resp = bus_initiator_socket->nb_transport_fw(transaction, phase, delay);

  if (tlm_resp == TLM_UPDATED) {
    wait(delay);
  }

  wait(bus_transaction_done);
}

void InterconnectProtocol::send_irq(tlm_generic_payload &transaction,
                                    tlm_command command) {
  ChipletExtension *ext;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  transaction.get_extension(ext);

  unsigned int flit_data_size = get_available_data_bytes_per_flit(transaction);
  unsigned int data_size =
      (ext->flit_count * flit_data_size) - ext->flit_padding;

  unsigned int address_offset = (ext->flit_count - 1) * flit_data_size;

  auto *irq = new ChipletPayload();

  irq->set_command(command);
  irq->set_address(transaction.get_address() - address_offset);
  irq->set_data_length(data_size);
  irq->set_request_id(ext->request_id);
  irq->set_core_id(ext->core_id);
  irq->set_source_id(ext->source_id);
  irq->set_destination_id(ext->destination_id);

  if (command == TLM_READ_COMMAND) {
    // send read IRQs to request core
    SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core" << ext->core_id);
    tlm_resp = irq_initiator_sockets[ext->core_id]->nb_transport_fw(*irq, phase,
                                                                    delay);
  } else {
    // send write IRQs to Core0
    SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core0");
    tlm_resp = irq_initiator_sockets[0]->nb_transport_fw(*irq, phase, delay);
  }

  if (tlm_resp == TLM_COMPLETED) {
    wait(delay);
  }

  SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core done");

  delete irq;
}

void InterconnectProtocol::process_queue() {
  tlm_generic_payload *next_transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  // dequeue next waiting request
  InterconnectRequest next_request = request_queue.front();
  request_queue.pop_front();

  // grant access
  current_interconnect = next_request.interconnect_id;
  next_transaction = next_request.transaction;
  next_transaction->get_extension(ext);

  delay = get_interconnect2protocol_process_delay(*this, *next_transaction,
                                                  post_delay);

  SC_LOG_DEBUG(this, *next_transaction,
               "Granting Protocol Layer access to Interconnect"
                   << current_interconnect << " from queue");

  peq_phy.notify(*next_transaction, delay);

  phase = END_REQ;
  delay = SC_ZERO_TIME;

  // end request
  interconnect_target_sockets[current_interconnect]->nb_transport_bw(
      *next_transaction, phase, delay);
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
InterconnectProtocol::nb_transport_fw_bus(tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received request from Bus");

  // add bus transfer delay
  delay +=
      get_bus_transfer_fw_delay(*this, transaction, bus_clk_cycle, bus_width);

  peq_bus.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum InterconnectProtocol::nb_transport_fw_interconnect(
    int id, tlm_generic_payload &transaction, tlm_phase &phase,
    sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "PROTOCOL: Received request from Interconnect" << id);
  utilization_tracker.set_active();

  if (current_interconnect == -1 && request_queue.empty()) {
    // protocol layer is free and queue is empty: grant access immediately
    SC_LOG_DEBUG(this, transaction,
                 "Protocol Layer is empty -> granting access to Interconnect"
                     << id);

    current_interconnect = id;

    // add interconnect to protocol layer process delay
    delay +=
        get_interconnect2protocol_process_delay(*this, transaction, post_delay);

    peq_phy.notify(transaction, delay);

    phase = END_REQ;
    return TLM_UPDATED;
  } else {
    // protocol layer is busy or queue is not empty: enqueue request
    SC_LOG_DEBUG(this, transaction,
                 "Protocol Layer is busy with Interconnect"
                     << current_interconnect
                     << " -> enqueuing request from Interconnect" << id);

    request_queue.push_back({id, &transaction});

    return TLM_ACCEPTED;
  }
}

tlm_sync_enum
InterconnectProtocol::nb_transport_bw_bus(tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received response from Bus");

    delay +=
        get_bus_transfer_bw_delay(*this, transaction, bus_clk_cycle, bus_width);

    bus_transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum InterconnectProtocol::nb_transport_bw_interconnect(
    int id, tlm_generic_payload &transaction, tlm_phase &phase,
    sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction,
                 "PROTOCOL: Received response from Interconnect" << id);

    phy_transaction_done.notify(SC_ZERO_TIME);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}