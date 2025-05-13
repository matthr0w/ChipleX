#include "InterconnectProtocol.h"
#include "Config.h"

#include "common/Delays.h"
#include "common/RoutingTable.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

chiplet::InterconnectProtocol::InterconnectProtocol(sc_module_name name,
                                                    unsigned int chiplet_id)
    : sc_module(name), chiplet_id(chiplet_id),
      bus_target_socket("bus_target_socket"),
      bus_initiator_socket("bus_initiator_socket"),
      core0_irq_initiator_socket("core0_irq_initiator_socket"),
      core1_irq_initiator_socket("core1_irq_initiator_socket") {

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

  write_address = (Config::instance().ramSize() * 1024) / 2;

  SC_THREAD(process_tx_buffer);
  SC_THREAD(process_rx_buffer);
}

chiplet::InterconnectProtocol::~InterconnectProtocol() {
  delete[] interconnect_target_sockets;
  delete[] interconnect_initiator_sockets;
}

void chiplet::InterconnectProtocol::process_tx_buffer() {
  while (true) {
    wait(tx_buffer_in_event);

    while (!tx_buffer.empty()) {
      tlm_generic_payload *transaction = tx_buffer.front();
      ChipletExtension *ext;

      transaction->get_extension(ext);

      // calculate crc
      if (!prepend_crc(*transaction)) {
        SC_LOG_ERROR(this, static_cast<ChipletPayload &>(*transaction),
                     "CRC calculation failed!");
      };

      send_to_interconnect(*transaction);

      // remove from tx buffer
      tx_buffer.pop_front();
      tx_buffer_out_event.notify();

      delete transaction;
    }
  }
}

void chiplet::InterconnectProtocol::process_rx_buffer() {
  while (true) {
    wait(rx_buffer_in_event);

    while (!rx_buffer.empty()) {
      tlm_generic_payload *transaction = rx_buffer.front();
      ChipletExtension *ext;

      transaction->get_extension(ext);

      bool at_source = ext->source_id == chiplet_id;
      bool at_destination = ext->destination_id == chiplet_id;

      bool read_op = transaction->get_command() == TLM_READ_COMMAND;
      bool write_op = transaction->get_command() == TLM_WRITE_COMMAND;

      // at source and read operation:
      //    transaction was an off-chip read request
      //    -> set to write operation, set write address, send to RAM via bus
      //    -> send irq to core
      // at destination and read operation:
      //    transaction is an off-chip read request
      //    -> send to RAM via bus
      //    -> send back to source via interconnects
      // at destination and write operation:
      //    transaction is an off-chip write request
      //    -> set write address, and send to RAM via bus
      // not at source or destination
      //    transaction is not at the destination
      //    -> send to destination via interconnects

      if (at_source && read_op) {
        transaction->set_command(TLM_WRITE_COMMAND);
        set_write_address(*transaction);
        process_bus_transaction(*transaction);
        send_irq(*transaction);
      } else if (at_destination) {
        if (read_op) {
          process_bus_transaction(*transaction);
          send_to_interconnect(*transaction);
        } else if (write_op) {
          set_write_address(*transaction);
          process_bus_transaction(*transaction);
        }
      } else {
        send_to_interconnect(*transaction);
      }

      // remove from rx buffer
      rx_buffer.pop_front();
      rx_buffer_out_event.notify();

      delete transaction;
    }
  }
}

void chiplet::InterconnectProtocol::process_bus_transaction(
    tlm_generic_payload &transaction) {
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  // remove crc
  if (!remove_crc(transaction)) {
    SC_LOG_ERROR(this, transaction, "CRC removal failed!");
  };

  SC_LOG_DEBUG(this, transaction, "Protocol->Bus transmission started");

  tlm_resp = bus_initiator_socket->nb_transport_fw(transaction, phase, delay);
  if (tlm_resp == TLM_UPDATED) {
    wait(delay);
  }

  wait(rx_transaction_done);

  // calculate crc
  if (!prepend_crc(transaction)) {
    SC_LOG_ERROR(this, transaction, "CRC calculation failed!");
  };

  SC_LOG_DEBUG(this, transaction, "Protocol->Bus transmission finished");
}

void chiplet::InterconnectProtocol::send_to_interconnect(
    tlm_generic_payload &transaction) {
  ChipletExtension *ext;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  transaction.get_extension(ext);

  auto *transaction_copy = static_cast<ChipletPayload &>(transaction).clone();

  int route = RoutingTable::get_route(chiplet_id, ext->destination_id);

  SC_LOG_WARN(this, *transaction_copy,
              "Chiplet ID " << chiplet_id << " Destination "
                            << ext->destination_id << " Route to Interconnect"
                            << route);

  SC_LOG_DEBUG(this, *transaction_copy,
               "Protocol->Interconnect" << route << " transmission started");

  tlm_resp = interconnect_initiator_sockets[route]->nb_transport_fw(
      *transaction_copy, phase, delay);
  if (tlm_resp == TLM_COMPLETED) {
    wait(delay);
  }

  SC_LOG_DEBUG(this, *transaction_copy,
               "Protocol->Interconnect" << route << " transmission finished");

  delete transaction_copy;
}

void chiplet::InterconnectProtocol::send_irq(tlm_generic_payload &transaction) {
  auto *irq = new ChipletPayload();
  ChipletExtension *ext;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  transaction.get_extension(ext);

  irq->set_command(TLM_IGNORE_COMMAND);
  irq->set_address(transaction.get_address());
  irq->set_data_length(transaction.get_data_length());

  irq->set_request_id(ext->request_id);
  irq->set_source_id(ext->source_id);
  irq->set_core_id(ext->core_id);
  irq->set_destination_id(ext->destination_id);

  SC_LOG_WARN(this, transaction, "Sending IRQ to Core" << ext->core_id);

  if (ext->core_id == 0) {
    tlm_resp = core0_irq_initiator_socket->nb_transport_fw(*irq, phase, delay);
  } else if (ext->core_id == 1) {
    tlm_resp = core1_irq_initiator_socket->nb_transport_fw(*irq, phase, delay);
  }

  if (tlm_resp == TLM_COMPLETED) {
    wait(delay);
  }

  SC_LOG_WARN(this, transaction,
              "Sending IRQ to Core" << ext->core_id << " done");

  delete irq;
}

// -------------------------------------------------------
// protocol functions
// -------------------------------------------------------
// TODO: add delays
uint16_t chiplet::InterconnectProtocol::calculate_crc16(const uint8_t *data,
                                                        size_t length) {
  // https://github.com/jpralves/crc16/blob/master/crc16.cpp
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int j = 0; j < 8; ++j) {
      if (crc & 0x8000)
        crc = (crc << 1) ^ 0x1021;
      else
        crc <<= 1;
    }
  }
  return crc;
}

bool chiplet::InterconnectProtocol::prepend_crc(
    tlm::tlm_generic_payload &transaction) {
  unsigned char *data = transaction.get_data_ptr();
  unsigned int length = transaction.get_data_length();

  uint16_t crc = calculate_crc16(data, length);

  // allocate new buffer
  unsigned int new_length = length + 2;
  unsigned char *new_data = new unsigned char[new_length];

  new_data[0] = crc >> 8;
  new_data[1] = crc & 0xFF;
  std::memcpy(new_data + 2, data, length);

  transaction.set_data_ptr(new_data);
  transaction.set_data_length(new_length);

  return true;
}

bool chiplet::InterconnectProtocol::remove_crc(
    tlm::tlm_generic_payload &transaction) {
  unsigned char *data = transaction.get_data_ptr();
  unsigned int length = transaction.get_data_length();

  uint16_t received_crc = (data[0] << 8) | data[1];
  unsigned char *actual_data = data + 2;
  unsigned int actual_length = length - 2;

  uint16_t computed_crc = calculate_crc16(actual_data, actual_length);
  if (received_crc != computed_crc) {
    SC_LOG_ERROR(this, transaction, "CRC mismatch!");
    return false;
  }

  unsigned char *stripped_data = new unsigned char[actual_length];
  std::memcpy(stripped_data, actual_data, actual_length);

  delete[] data;

  transaction.set_data_ptr(stripped_data);
  transaction.set_data_length(actual_length);

  return true;
}

void chiplet::InterconnectProtocol::set_write_address(
    tlm_generic_payload &transaction) {
  SC_LOG_DEBUG(this, transaction,
               "Setting write address to: " << std::hex << write_address);
  transaction.set_address(write_address);
  write_address += sizeof(uint32_t);
  if (write_address >= Config::instance().ramSize() * 1024) {
    write_address = (Config::instance().ramSize() * 1024) / 2;
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum chiplet::InterconnectProtocol::nb_transport_fw_bus(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "Received request from Bus");

  output_buffer_levels();

  auto *transaction_copy = static_cast<ChipletPayload *>(&transaction)->clone();

  if (tx_buffer.size() == Config::instance().interconnectProtocolBufferSize()) {
    SC_LOG_WARN(this, transaction, "Tx buffer full -> waiting...");
    wait(tx_buffer_out_event);
  }

  // add bus transfer delay
  delay += get_bus_transfer_fw_delay(*this, transaction,
                                     Config::instance().busClkCycle(),
                                     Config::instance().busWidth());

  // set source id
  ChipletExtension *ext;
  transaction.get_extension(ext);
  if (ext->source_id == -1) {
    static_cast<ChipletPayload *>(&transaction)->set_source_id(chiplet_id);
    transaction_copy->set_source_id(chiplet_id);
  }

  // put transaction in tx buffer
  SC_LOG_DEBUG(this, transaction, "Write transaction in Tx buffer");
  tx_buffer.push_back(transaction_copy);
  tx_buffer_in_event.notify(delay);

  // begin response to bus
  tlm_phase resp_phase = BEGIN_RESP;
  sc_time resp_delay = delay;

  bus_target_socket->nb_transport_bw(transaction, resp_phase, resp_delay);

  phase = END_REQ;
  return TLM_COMPLETED;
}

tlm_sync_enum chiplet::InterconnectProtocol::nb_transport_fw_interconnect(
    int id, tlm_generic_payload &transaction, tlm_phase &phase,
    sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "Received request from Interconnect" << id);

  output_buffer_levels();

  auto *transaction_copy = static_cast<ChipletPayload *>(&transaction)->clone();

  if (rx_buffer.size() == Config::instance().interconnectProtocolBufferSize()) {
    SC_LOG_WARN(this, transaction, "Rx buffer full -> waiting...");
    wait(rx_buffer_out_event);
  }

  // add interconnect to protocol layer transfer delay
  delay += get_protocol2interconnect_transfer_delay(
      *this, transaction, Config::instance().interconnectProtocolClkCycle(),
      Config::instance().interconnectProtocolWidth());

  // put transaction in rx buffer
  SC_LOG_DEBUG(this, transaction, "Write transaction in Rx buffer");
  rx_buffer.push_back(transaction_copy);
  rx_buffer_in_event.notify(delay);

  // begin response to interconnect
  tlm_phase resp_phase = BEGIN_RESP;
  sc_time resp_delay = delay;

  interconnect_target_sockets[id]->nb_transport_bw(transaction, resp_phase,
                                                   resp_delay);

  phase = END_REQ;
  return TLM_COMPLETED;
}

tlm_sync_enum chiplet::InterconnectProtocol::nb_transport_bw_bus(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction, "Received response from Bus");

    delay += get_bus_transfer_bw_delay(*this, transaction,
                                       Config::instance().busClkCycle(),
                                       Config::instance().busWidth());

    rx_transaction_done.notify(delay);

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
                 "Received response from Interconnect" << id);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

// helper functions
void chiplet::InterconnectProtocol::output_buffer_levels() {
  SC_LOG_DEBUG_NO_TX(this, "Buffer Levels");
  SC_LOG_DEBUG_NO_TX(this, "Tx buffer: " << tx_buffer.size());
  SC_LOG_DEBUG_NO_TX(this, "Rx buffer: " << rx_buffer.size());
}