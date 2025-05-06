#include "InterconnectProtocol.h"
#include "Config.h"

#include "common/Delays.h"
#include "common/protocol/ChipletPayload.h"

#include "include/globals.h"
#include "include/logging.h"

fpga::InterconnectProtocol::InterconnectProtocol(sc_module_name name,
                                                 unsigned int fpga_id)
    : sc_module(name), fpga_id(fpga_id), bus_target_socket("bus_target_socket"),
      bus_initiator_socket("bus_initiator_socket"),
      generator_irq_initiator_socket("generator_irq_initiator_socket") {

  bus_target_socket.register_nb_transport_fw(
      this, &fpga::InterconnectProtocol::nb_transport_fw_bus);
  bus_initiator_socket.register_nb_transport_bw(
      this, &fpga::InterconnectProtocol::nb_transport_bw_bus);

  interconnect_target_sockets =
      new simple_target_socket_tagged<fpga::InterconnectProtocol>[num_chiplets];
  interconnect_initiator_sockets = new simple_initiator_socket_tagged<
      fpga::InterconnectProtocol>[num_chiplets];

  for (unsigned int i = 0; i < num_chiplets; ++i) {
    interconnect_target_sockets[i].register_nb_transport_fw(
        this, &fpga::InterconnectProtocol::nb_transport_fw_interconnect, i);
    interconnect_initiator_sockets[i].register_nb_transport_bw(
        this, &fpga::InterconnectProtocol::nb_transport_bw_interconnect, i);
  }

  write_address = (RAM_SIZE * 1024) / 2;

  SC_THREAD(process_tx_buffer);
  SC_THREAD(process_rx_buffer);
}

fpga::InterconnectProtocol::~InterconnectProtocol() {
  delete[] interconnect_target_sockets;
  delete[] interconnect_initiator_sockets;
}

void fpga::InterconnectProtocol::process_tx_buffer() {
  while (true) {
    wait(tx_buffer_in_event);

    while (!tx_buffer.empty()) {
      tlm_generic_payload *transaction = tx_buffer.front();
      ChipletExtension *ext;

      transaction->get_extension(ext);

      // entry point for protocol methods

      send_to_interconnect(*transaction);

      // remove from tx buffer
      tx_buffer.pop_front();
      tx_buffer_out_event.notify();

      delete transaction;
    }
  }
}

void fpga::InterconnectProtocol::process_rx_buffer() {
  while (true) {
    wait(rx_buffer_in_event);

    while (!rx_buffer.empty()) {
      tlm_generic_payload *transaction = rx_buffer.front();
      ChipletExtension *ext;

      transaction->get_extension(ext);

      bool at_source = ext->source_id == fpga_id;
      bool at_destination = ext->destination_id == fpga_id;

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

void fpga::InterconnectProtocol::set_write_address(
    tlm_generic_payload &transaction) {
  SC_LOG_DEBUG(this, transaction,
               "Setting write address to: " << std::hex << write_address);
  transaction.set_address(write_address);
  write_address += sizeof(uint32_t);
  if (write_address >= RAM_SIZE * 1024)
    write_address = (RAM_SIZE * 1024) / 2;
}

void fpga::InterconnectProtocol::process_bus_transaction(
    tlm_generic_payload &transaction) {
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  SC_LOG_DEBUG(this, transaction, "Protocol->Bus transmission started");

  tlm_resp = bus_initiator_socket->nb_transport_fw(transaction, phase, delay);
  if (tlm_resp == TLM_UPDATED) {
    wait(delay);
  }

  wait(rx_transaction_done);

  SC_LOG_DEBUG(this, transaction, "Protocol->Bus transmission finished");
}

void fpga::InterconnectProtocol::send_to_interconnect(
    tlm_generic_payload &transaction) {
  ChipletExtension *ext;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  transaction.get_extension(ext);

  auto *transaction_copy = static_cast<ChipletPayload &>(transaction).clone();

  SC_LOG_DEBUG(this, *transaction_copy,
               "Protocol->Interconnect" << ext->destination_id
                                        << " transmission started");

  tlm_resp =
      interconnect_initiator_sockets[ext->destination_id - 1]->nb_transport_fw(
          *transaction_copy, phase, delay);
  if (tlm_resp == TLM_COMPLETED) {
    wait(delay);
  }

  SC_LOG_DEBUG(this, *transaction_copy,
               "Protocol->Interconnect" << ext->destination_id
                                        << " transmission finished");
}

void fpga::InterconnectProtocol::send_irq(tlm_generic_payload &transaction) {
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

  SC_LOG_WARN(this, transaction, "Sending IRQ to Generator" << ext->core_id);

  tlm_resp =
      generator_irq_initiator_socket->nb_transport_fw(*irq, phase, delay);

  if (tlm_resp == TLM_COMPLETED) {
    wait(delay);
  }

  delete irq;

  SC_LOG_WARN(this, transaction, "Sending IRQ to Generator done");
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum fpga::InterconnectProtocol::nb_transport_fw_bus(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "Received request from Bus");

  output_buffer_levels();

  auto *transaction_copy = static_cast<ChipletPayload *>(&transaction)->clone();

  if (tx_buffer.size() == INTERCONNECT_PROTOCOL_BUFFER_SIZE) {
    SC_LOG_WARN(this, transaction, "Tx buffer full -> waiting...");
    wait(tx_buffer_out_event);
  }

  // add bus transfer delay
  delay +=
      get_bus_transfer_fw_delay(*this, transaction, BUS_CLK_CYCLE, BUS_WIDTH);

  // set source id
  ChipletExtension *ext;
  transaction.get_extension(ext);
  if (ext->source_id == -1) {
    static_cast<ChipletPayload *>(&transaction)->set_source_id(fpga_id);
    transaction_copy->set_source_id(fpga_id);
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

tlm_sync_enum fpga::InterconnectProtocol::nb_transport_fw_interconnect(
    int id, tlm_generic_payload &transaction, tlm_phase &phase,
    sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "Received request from Interconnect" << id);

  output_buffer_levels();

  auto *transaction_copy = static_cast<ChipletPayload *>(&transaction)->clone();

  if (rx_buffer.size() == INTERCONNECT_PROTOCOL_BUFFER_SIZE) {
    SC_LOG_WARN(this, transaction, "Rx buffer full -> waiting...");
    wait(rx_buffer_out_event);
  }

  // add interconnect to protocol layer transfer delay
  delay += get_protocol2interconnect_transfer_delay(
      *this, transaction, INTERCONNECT_PROTOCOL_CLK_CYCLE,
      INTERCONNECT_PROTOCOL_WIDTH);

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

tlm_sync_enum fpga::InterconnectProtocol::nb_transport_bw_bus(
    tlm_generic_payload &transaction, tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction, "Received response from Bus");

    delay +=
        get_bus_transfer_bw_delay(*this, transaction, BUS_CLK_CYCLE, BUS_WIDTH);

    rx_transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum fpga::InterconnectProtocol::nb_transport_bw_interconnect(
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
void fpga::InterconnectProtocol::output_buffer_levels() {
  SC_LOG_DEBUG_NO_TX(this, "Buffer Levels");
  SC_LOG_DEBUG_NO_TX(this, "Tx buffer: " << tx_buffer.size());
  SC_LOG_DEBUG_NO_TX(this, "Rx buffer: " << rx_buffer.size());
}