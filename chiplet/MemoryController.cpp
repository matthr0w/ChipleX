#include "MemoryController.h"

#include "common/Delays.h"
#include "common/Flits.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

chiplet::MemoryController::MemoryController(sc_module_name name)
    : sc_module(name), bus_target_socket("bus_target_socket"),
      ram_initiator_socket("ram_initiator_socket"), peq("peq") {
  bus_target_socket.register_nb_transport_fw(
      this, &chiplet::MemoryController::nb_transport_fw);
  ram_initiator_socket.register_nb_transport_bw(
      this, &chiplet::MemoryController::nb_transport_bw);

  offchip_base_address = (ram_size * 1024) / 2;

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void chiplet::MemoryController::process_transaction() {
  ChipletExtension *ext;
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq.get_next_transaction();

    // set address
    set_address(*transaction);

    // send to RAM
    send_to_ram(*transaction);

    // begin response to bus
    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp = bus_target_socket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }
  }
}

void chiplet::MemoryController::send_to_ram(tlm_generic_payload &transaction) {
  ChipletExtension *ext;
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  tlm_resp = ram_initiator_socket->nb_transport_fw(transaction, phase, delay);

  if (tlm_resp == TLM_UPDATED) {
    wait(delay);
  }

  wait(ram_transaction_done);
}

void chiplet::MemoryController::set_address(
    tlm::tlm_generic_payload &transaction) {
  ChipletExtension *ext;
  transaction.get_extension(ext);

  uint32_t address = transaction.get_address();
  unsigned int data_size = transaction.get_data_length();

  bool is_onchip =
      ext->source_id == -1 || ext->source_id == ext->destination_id;

  bool read_op = transaction.get_command() == TLM_READ_COMMAND;
  bool write_op = transaction.get_command() == TLM_WRITE_COMMAND;

  if (is_onchip) {
    if (read_op) {
      // on-chip read request
      // free allocated addresses on read
      allocated_ranges.erase(address);
    } else if (write_op && ext->flit_id != -1) {
      // off-chip read response
      uint32_t dynamic_address =
          allocate_dynamic_address(transaction, true, data_size);
      transaction.set_address(dynamic_address);
    } else if (write_op && !ext->fixed_address) {
      // on-chip write, dynamic
      uint32_t dynamic_address =
          allocate_dynamic_address(transaction, true, data_size);
      transaction.set_address(dynamic_address);
    }
    // else: write_op && ext->fixed_address -> do nothing
  } else {
    if (read_op) {
      // off-chip read request
      transaction.set_address(address + offchip_base_address);
      // free allocated addresses on read
      allocated_ranges.erase(address + offchip_base_address);
      // for read response: source becomes destination
      static_cast<ChipletPayload *>(&transaction)
          ->set_destination_id(ext->source_id);
    } else if (write_op && ext->fixed_address) {
      transaction.set_address(address + offchip_base_address);
    } else if (write_op && !ext->fixed_address) {
      const int request_id = ext->request_id;
      const unsigned int flit_data_size =
          get_available_data_bytes_per_flit(transaction);
      const unsigned int request_data_size = flit_data_size * ext->flit_count;

      if (ext->flit_id == 0) {
        // allocate dynamic address for first flit
        uint32_t flit_base_address =
            allocate_dynamic_address(transaction, false, request_data_size);
        pending_flit_writes[request_id] = flit_base_address;
        transaction.set_address(flit_base_address);
      } else {
        // increment dynamic address for upcoming flits
        auto it = pending_flit_writes.find(request_id);

        uint32_t flit_base_address = it->second;
        uint32_t flit_address =
            flit_base_address + ext->flit_id * flit_data_size;

        transaction.set_address(flit_address);

        if (ext->flit_id == ext->flit_count - 1) {
          // remove pending on last flit
          pending_flit_writes.erase(it);
        }
      }
    }
  }
}

uint32_t chiplet::MemoryController::allocate_dynamic_address(
    tlm_generic_payload &transaction, bool onchip, uint32_t size) {
  uint32_t base_address = onchip ? 0 : offchip_base_address;
  uint32_t max_address = onchip ? offchip_base_address : ram_size * 1024;

  uint32_t address = base_address;
  for (const auto &[start, len] : allocated_ranges) {
    if (address + size <= start) {
      break;
    }
    address = start + len;
  }

  if (address + size > max_address) {
    SC_LOG_WARN(this, transaction,
                 "Out of memory for dynamic address allocation");
  }

  allocated_ranges[address] = size;
  return address;
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
chiplet::MemoryController::nb_transport_fw(tlm_generic_payload &transaction,
                                           tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received request from Bus");

  // add address assignment delay
  delay +=
      get_bus_transfer_fw_delay(*this, transaction, bus_clk_cycle, bus_width);

  peq.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum
chiplet::MemoryController::nb_transport_bw(tlm_generic_payload &transaction,
                                           tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received response from RAM");

    delay += SC_ZERO_TIME;

    ram_transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}