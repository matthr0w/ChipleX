#include "InterconnectProtocol.h"

#include "common/Delays.h"
#include "common/RoutingTable.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

chiplet::InterconnectProtocol::InterconnectProtocol(sc_module_name name,
                                                    unsigned int chiplet_id)
    : sc_module(name), chiplet_id(chiplet_id),
      bus_target_socket("bus_target_socket"),
      bus_initiator_socket("bus_initiator_socket"),
      core0_irq_initiator_socket("core0_irq_initiator_socket"),
      core1_irq_initiator_socket("core1_irq_initiator_socket"),
      peq_bus("peq_bus"), peq_phy("peq_phy") {
  bus_target_socket.register_nb_transport_fw(
      this, &chiplet::InterconnectProtocol::nb_transport_fw_bus);
  bus_initiator_socket.register_nb_transport_bw(
      this, &chiplet::InterconnectProtocol::nb_transport_bw_bus);

  interconnect_target_sockets =
      new simple_target_socket_tagged<chiplet::InterconnectProtocol>[3];
  interconnect_initiator_sockets =
      new simple_initiator_socket_tagged<chiplet::InterconnectProtocol>[3];

  for (unsigned int i = 0; i < 3; ++i) {
    interconnect_target_sockets[i].register_nb_transport_fw(
        this, &chiplet::InterconnectProtocol::nb_transport_fw_interconnect, i);
    interconnect_initiator_sockets[i].register_nb_transport_bw(
        this, &chiplet::InterconnectProtocol::nb_transport_bw_interconnect, i);
  }

  write_address = (chiplet_config.get<unsigned int>("ram.size") * 1024) / 2;

  SC_THREAD(process_bus_transaction);
  sensitive << peq_bus.get_event();
  SC_THREAD(process_phy_transaction);
  sensitive << peq_phy.get_event();
}

chiplet::InterconnectProtocol::~InterconnectProtocol() {
  delete[] interconnect_target_sockets;
  delete[] interconnect_initiator_sockets;
}

void chiplet::InterconnectProtocol::process_bus_transaction() {
  ChipletExtension *ext;
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq_bus.get_next_transaction();

    // set source id
    transaction->get_extension(ext);
    if (ext->source_id == -1) {
      static_cast<ChipletPayload *>(transaction)->set_source_id(chiplet_id);
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
  }
}

void chiplet::InterconnectProtocol::process_phy_transaction() {
  ChipletExtension *ext;
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq_phy.get_next_transaction();
    transaction->get_extension(ext);

    bool at_source = ext->source_id == chiplet_id;
    bool at_destination = ext->destination_id == chiplet_id;

    bool read_op = transaction->get_command() == TLM_READ_COMMAND;
    bool write_op = transaction->get_command() == TLM_WRITE_COMMAND;

    int flit_count = ext->flit_count;
    int flit_id = ext->flit_id;

    // at source and read operation:
    //    transaction was an off-chip read request
    //    -> set to write operation, set write address, send to RAM via bus
    //    -> send IRQ to core
    // at destination and read operation:
    //    transaction is an off-chip read request
    //    -> send to RAM via bus
    //    -> send back to source via interconnects
    // at destination and write operation:
    //    transaction is an off-chip write request
    //    -> set write address, send to RAM via bus
    //    -> send IRQ to core
    // not at source or destination
    //    transaction is not at the destination
    //    -> send to destination via interconnects

    if (at_source && read_op) {
      transaction->set_command(TLM_WRITE_COMMAND);
      set_write_address(*transaction);
      send_to_bus(*transaction);
      if (flit_id == flit_count - 1)
        send_irq(*transaction, TLM_READ_COMMAND);
    } else if (at_destination) {
      if (read_op) {
        send_to_bus(*transaction);
        send_to_phy(*transaction);
      } else if (write_op) {
        set_write_address(*transaction);
        send_to_bus(*transaction);
        if (flit_id == flit_count - 1)
          send_irq(*transaction, TLM_WRITE_COMMAND);
      }
    } else {
      send_to_phy(*transaction);
    }

    // begin response to interconnect
    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    int id = -1;

    auto it = transaction_id_map.find(transaction);
    if (it != transaction_id_map.end()) {
      id = it->second;
    }

    transaction_id_map.erase(transaction);

    tlm_resp = interconnect_target_sockets[id]->nb_transport_bw(*transaction,
                                                                phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }
  }
}

void chiplet::InterconnectProtocol::send_to_bus(
    tlm_generic_payload &transaction) {
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  tlm_resp = bus_initiator_socket->nb_transport_fw(transaction, phase, delay);

  if (tlm_resp == TLM_UPDATED) {
    wait(delay);
  }

  wait(bus_transaction_done);
}

void chiplet::InterconnectProtocol::send_irq(tlm_generic_payload &transaction,
                                             tlm_command command) {
  auto *irq = new ChipletPayload();
  ChipletExtension *ext;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  transaction.get_extension(ext);

  irq->set_command(command);

  irq->set_request_id(ext->request_id);
  irq->set_source_id(ext->source_id);
  irq->set_core_id(ext->core_id);
  irq->set_destination_id(ext->destination_id);

  if (command == TLM_READ_COMMAND) {
    unsigned int data_bytes_per_flit = flit_size;
    data_bytes_per_flit -= header_size;
    data_bytes_per_flit -= ext->get_size_bytes();
    data_bytes_per_flit -= ext->get_protocol_size_bytes();

    irq->set_address(transaction.get_address() -
                     ext->flit_count * data_bytes_per_flit);
    irq->set_data_length((ext->flit_count * data_bytes_per_flit) -
                         ext->flit_padding);

    // send read IRQs to request core
    SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core" << ext->core_id);
    if (ext->core_id == 0) {
      tlm_resp =
          core0_irq_initiator_socket->nb_transport_fw(*irq, phase, delay);
    } else if (ext->core_id == 1) {
      tlm_resp =
          core1_irq_initiator_socket->nb_transport_fw(*irq, phase, delay);
    }
  } else {
    unsigned int data_bytes_per_flit = flit_size;
    data_bytes_per_flit -= header_size;
    data_bytes_per_flit -= ext->get_size_bytes();
    data_bytes_per_flit -= ext->get_protocol_size_bytes();
    data_bytes_per_flit -= sizeof(uint32_t);

    irq->set_address(transaction.get_address() -
                     (ext->flit_count - 1) * data_bytes_per_flit);
    irq->set_data_length((ext->flit_count * data_bytes_per_flit) -
                         ext->flit_padding);

    // send write IRQs to Core0
    SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core0");
    tlm_resp = core0_irq_initiator_socket->nb_transport_fw(*irq, phase, delay);
  }

  if (tlm_resp == TLM_COMPLETED) {
    wait(delay);
  }

  SC_LOG_DEBUG(this, transaction, "Sending IRQ to core done");

  delete irq;
}

// -------------------------------------------------------
// protocol functions
// -------------------------------------------------------
void chiplet::InterconnectProtocol::send_flits(
    tlm_generic_payload &transaction) {

  auto total_size = transaction.get_data_length();
  auto address = transaction.get_address();
  unsigned char *data_ptr = transaction.get_data_ptr();

  unsigned int flit_count = get_required_flit_count(transaction);
  unsigned int data_bytes_size = get_available_data_bytes_per_flit(transaction);

  unsigned int flit_id = 0;
  unsigned int offset = 0;
  while (offset < total_size) {
    unsigned int current_data_size =
        std::min(data_bytes_size, total_size - offset);

    auto *flit = static_cast<ChipletPayload &>(transaction).clone_ext();

    unsigned char *flit_data = new unsigned char[data_bytes_size]();

    std::memcpy(flit_data, data_ptr + offset, current_data_size);

    flit->set_command(transaction.get_command());
    flit->set_address(address + offset);
    flit->set_data_ptr(flit_data);
    flit->set_data_length(current_data_size);
    flit->set_flit_count(flit_count);
    flit->set_flit_id(flit_id);
    flit->set_flit_padding(data_bytes_size - current_data_size);

    send_to_phy(*flit);

    delete flit;

    flit_id += 1;
    offset += current_data_size;
  }
}

void chiplet::InterconnectProtocol::send_to_phy(
    tlm_generic_payload &transaction) {
  ChipletExtension *ext;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  transaction.get_extension(ext);

  int route = RoutingTable::get_route(chiplet_id, ext->destination_id);

  SC_LOG_DEBUG(this, transaction,
               "ROUTING: Chiplet ID " << chiplet_id << " Destination "
                                      << ext->destination_id
                                      << " Route to Interconnect" << route);

  tlm_resp = interconnect_initiator_sockets[route]->nb_transport_fw(
      transaction, phase, delay);

  if (tlm_resp == TLM_UPDATED) {
    wait(delay);
  }

  wait(phy_transaction_done);
}

void chiplet::InterconnectProtocol::set_write_address(
    tlm_generic_payload &transaction) {
  SC_LOG_DEBUG(this, transaction,
               "Setting write address to: " << std::hex << write_address);
  transaction.set_address(write_address);
  ChipletExtension *ext;
  transaction.get_extension(ext);
  unsigned int data_bytes_per_flit =
      get_available_data_bytes_per_flit(transaction);
  write_address += data_bytes_per_flit - ext->flit_padding;
  if (write_address >= chiplet_config.get<unsigned int>("ram.size") * 1024) {
    write_address = (chiplet_config.get<unsigned int>("ram.size") * 1024) / 2;
  }
}

// -------------------------------------------------------
// flit related functions
// -------------------------------------------------------
bool chiplet::InterconnectProtocol::is_request(const ChipletExtension *ext) {
  return ext && (ext->destination_id != ext->source_id);
}

unsigned int chiplet::InterconnectProtocol::get_required_flit_count(
    tlm_generic_payload &transaction) {
  unsigned int total_data_bytes = transaction.get_data_length();
  unsigned int available_data_bytes =
      get_available_data_bytes_per_flit(transaction);

  return (total_data_bytes + available_data_bytes - 1) / available_data_bytes;
}

unsigned int chiplet::InterconnectProtocol::get_available_data_bytes_per_flit(
    tlm_generic_payload &transaction) {
  ChipletExtension *ext;
  transaction.get_extension(ext);

  unsigned int size = flit_size;

  size -= header_size;

  size -= ext->get_size_bytes();
  size -= ext->get_protocol_size_bytes();

  if (is_request(ext)) {
    size -= sizeof(uint32_t);
  }

  return size;
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum chiplet::InterconnectProtocol::nb_transport_fw_bus(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received request from Bus");

  // add bus transfer delay
  delay += get_bus_transfer_fw_delay(
      *this, transaction, chiplet_config.get<sc_time>("bus.clk_cycle"),
      chiplet_config.get<unsigned int>("bus.width"));

  peq_bus.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum chiplet::InterconnectProtocol::nb_transport_fw_interconnect(
    int id, tlm_generic_payload &transaction, tlm_phase &phase,
    sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "PROTOCOL: Received request from Interconnect" << id);

  // add interconnect to protocol layer process delay
  delay += get_interconnect2protocol_process_delay(
      *this, transaction,
      interconnect_config.get<sc_time>("interconnect_protocol.post_delay"));

  transaction_id_map[&transaction] = id;

  peq_phy.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum chiplet::InterconnectProtocol::nb_transport_bw_bus(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received response from Bus");

    delay += get_bus_transfer_bw_delay(
        *this, transaction, chiplet_config.get<sc_time>("bus.clk_cycle"),
        chiplet_config.get<unsigned int>("bus.width"));

    bus_transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum chiplet::InterconnectProtocol::nb_transport_bw_interconnect(
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