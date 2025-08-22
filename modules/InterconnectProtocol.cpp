#include "InterconnectProtocol.h"

#include "common/Flits.h"
#include "common/RoutingTable.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

InterconnectProtocol::InterconnectProtocol(
    sc_module_name name, unsigned int chip_id, unsigned int num_cores,
    unsigned int num_interconnects, sc_time pre_delay, sc_time post_delay)
    : sc_module(name), chip_id(chip_id), pre_delay(pre_delay),
      post_delay(post_delay), utilization_tracker(this->name()),
      axi_peq("axi_peq"), phy_peq("phy_peq"), current_interconnect(-1) {
  axi_tsocket.register_nb_transport_fw(
      this, &InterconnectProtocol::nb_transport_fw_axi);
  axi_isocket.register_nb_transport_bw(
      this, &InterconnectProtocol::nb_transport_bw_axi);

  phy_tsockets =
      new simple_target_socket_tagged<InterconnectProtocol>[num_interconnects];
  phy_isockets = new simple_initiator_socket_tagged<
      InterconnectProtocol>[num_interconnects];

  for (unsigned int i = 0; i < num_interconnects; ++i) {
    phy_tsockets[i].register_nb_transport_fw(
        this, &InterconnectProtocol::nb_transport_fw_phy, i);
    phy_isockets[i].register_nb_transport_bw(
        this, &InterconnectProtocol::nb_transport_bw_phy, i);
  }

  irq_sockets =
      new simple_initiator_socket_tagged<InterconnectProtocol>[num_cores];

  SC_THREAD(process_axi_request_queue);
  SC_THREAD(process_axi_transaction);
  sensitive << axi_peq.get_event();

  SC_THREAD(process_phy_request_queue);
  SC_THREAD(process_phy_transaction);
  sensitive << phy_peq.get_event();
}

InterconnectProtocol::~InterconnectProtocol() {
  delete[] phy_tsockets;
  delete[] phy_isockets;
}

void InterconnectProtocol::process_axi_transaction() {
  while (true) {
    wait();

    tlm_generic_payload *transaction = axi_peq.get_next_transaction();
    ChipletExtension *ext = transaction->get_extension<ChipletExtension>();

    // set source id
    if (ext->source_id == -1) {
      static_cast<ChipletPayload *>(transaction)->set_source_id(chip_id);
    }

    utilization_tracker.set_active();
    send_flits(*transaction);
    utilization_tracker.set_idle();

    tlm_phase phase = BEGIN_RESP;
    sc_time delay = SC_ZERO_TIME;

    axi_tsocket->nb_transport_bw(*transaction, phase, delay);

    axi_resp_evt.notify(delay);
  }
}

void InterconnectProtocol::process_axi_request_queue() {
  while (true) {
    wait(axi_req_evt);

    while (!axi_request_queue.empty()) {
      AXIRequest request = axi_request_queue.front();
      axi_request_queue.pop_front();

      tlm_generic_payload *transaction = request.transaction;
      tlm_phase phase = UNINITIALIZED_PHASE;
      sc_time delay = *request.delay;

      axi_peq.notify(*transaction, delay);

      phase = END_REQ;
      delay = SC_ZERO_TIME;

      axi_tsocket->nb_transport_bw(*transaction, phase, delay);

      wait(axi_resp_evt);
    }
  }
}

void InterconnectProtocol::process_phy_transaction() {
  while (true) {
    wait();

    tlm_generic_payload *transaction = phy_peq.get_next_transaction();
    ChipletExtension *ext = transaction->get_extension<ChipletExtension>();

    bool at_source = ext->source_id == chip_id;
    bool at_destination = ext->destination_id == chip_id;

    bool read_op = transaction->is_read();
    bool write_op = transaction->is_write();

    int flit_count = ext->flit_count;
    int flit_id = ext->flit_id;

    // at source and read operation:
    //    transaction was an off-chip read request
    //    -> set to write operation, send to RAM via AXI
    //    -> send IRQ to core
    // at destination and read operation:
    //    transaction is an off-chip read request
    //    -> send to RAM via AXI
    //    -> send back to source via interconnects
    // at destination and write operation:
    //    transaction is an off-chip write request
    //    -> send to RAM via AXI
    //    -> send IRQ to core
    // not at source or destination
    //    transaction is not at the destination
    //    -> send to destination via interconnects

    if (at_source && read_op) {
      transaction->set_command(TLM_WRITE_COMMAND);
      send_axi_request(*transaction);
      if (flit_id == flit_count - 1) { // send IRQ on last flit
        send_irq(*transaction, TLM_READ_COMMAND);
      }
    } else if (at_destination) {
      if (read_op) {
        send_axi_request(*transaction);
        send_phy_request(*transaction);
      } else if (write_op) {
        send_axi_request(*transaction);
        if (flit_id == flit_count - 1) { // send IRQ on last flit
          send_irq(*transaction, TLM_WRITE_COMMAND);
        }
      }
    } else {
      send_phy_request(*transaction);
    }

    tlm_phase phase = BEGIN_RESP;
    sc_time delay = SC_ZERO_TIME;

    phy_tsockets[current_interconnect]->nb_transport_bw(*transaction, phase,
                                                        delay);

    phy_resp_evt.notify(delay);
  }
}

void InterconnectProtocol::process_phy_request_queue() {
  while (true) {
    wait(phy_req_evt);

    while (!phy_request_queue.empty()) {
      PHYRequest request = phy_request_queue.front();
      phy_request_queue.pop_front();

      tlm_generic_payload *transaction = request.transaction;
      tlm_phase phase = UNINITIALIZED_PHASE;
      sc_time delay = *request.delay;

      current_interconnect = request.interconnect_id;

      phy_peq.notify(*transaction, delay);

      wait(phy_resp_evt);
    }
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

    send_phy_request(*flit);

    delete flit;

    flit_id += 1;
    offset += current_data_size;
  }
}

void InterconnectProtocol::send_axi_request(tlm_generic_payload &transaction) {
  ChipletExtension *ext = nullptr;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  tlm_resp = axi_isocket->nb_transport_fw(transaction, phase, delay);

  if (tlm_resp == TLM_UPDATED) {
    wait(delay);
  }

  wait(axi_transaction_done);
}

void InterconnectProtocol::send_phy_request(tlm_generic_payload &transaction) {
  ChipletExtension *ext = nullptr;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  transaction.get_extension(ext);

  int route = RoutingTable::get_route(chip_id, ext->destination_id);

  if (route == -1) {
    SC_LOG_ERROR(this, transaction,
                 "ROUTING: Destination " << ext->destination_id
                                         << " not available.");
    return;
  }

  SC_LOG_DEBUG(this, transaction,
               "ROUTING: Chip ID " << chip_id << " Destination "
                                   << ext->destination_id
                                   << " Route to Interconnect" << route);

  tlm_resp = phy_isockets[route]->nb_transport_fw(transaction, phase, delay);

  if (tlm_resp == TLM_UPDATED) {
    wait(delay);
  }

  wait(phy_transaction_done);
}

void InterconnectProtocol::send_irq(tlm_generic_payload &transaction,
                                    tlm_command command) {
  ChipletExtension *ext = nullptr;
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
    tlm_resp = irq_sockets[ext->core_id]->nb_transport_fw(*irq, phase, delay);
  } else {
    // send write IRQs to Core0
    SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core0");
    tlm_resp = irq_sockets[0]->nb_transport_fw(*irq, phase, delay);
  }

  if (tlm_resp == TLM_COMPLETED) {
    wait(delay);
  }

  SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core done");

  delete irq;
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
InterconnectProtocol::nb_transport_fw_axi(tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase << " - AXI");

  switch (phase) {
  case BEGIN_REQ:
    delay += delays.pre_delay(transaction);

    axi_request_queue.push_back({&transaction, &phase, &delay});
    axi_req_evt.notify(SC_ZERO_TIME);

    return TLM_ACCEPTED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum
InterconnectProtocol::nb_transport_fw_phy(int id,
                                          tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase << " - PHY" << id);

  switch (phase) {
  case BEGIN_REQ:
    delay += delays.post_delay(transaction);

    phy_request_queue.push_back({id, &transaction, &phase, &delay});
    phy_req_evt.notify(SC_ZERO_TIME);

    return TLM_ACCEPTED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum
InterconnectProtocol::nb_transport_bw_axi(tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase << " - AXI");

  switch (phase) {
  case BEGIN_RESP:
    axi_transaction_done.notify(delay);
    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum
InterconnectProtocol::nb_transport_bw_phy(int id,
                                          tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase << " - PHY" << id);

  switch (phase) {
  case BEGIN_RESP:
    phy_transaction_done.notify(delay);
    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}