#include "MemoryController.h"

#include "common/Delays.h"
#include "common/Flits.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

MemoryController::MemoryController(sc_module_name name, unsigned int bus_width,
                                   sc_time bus_clk_cycle, unsigned int ram_size)
    : sc_module(name), bus_width(bus_width), bus_clk_cycle(bus_clk_cycle),
      ram_size(ram_size), utilization_tracker(this->name()),
      bus_target_socket("bus_target_socket"),
      ram_initiator_socket("ram_initiator_socket"), peq("peq") {
  bus_target_socket.register_nb_transport_fw(
      this, &MemoryController::nb_transport_fw);
  ram_initiator_socket.register_nb_transport_bw(
      this, &MemoryController::nb_transport_bw);

  offchip_base_address = (ram_size * 1024) / 2;

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void MemoryController::process_transaction() {
  ChipletExtension *ext;
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    utilization_tracker.set_active();

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

    utilization_tracker.set_idle();
  }
}

void MemoryController::send_to_ram(tlm_generic_payload &transaction) {
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

void MemoryController::set_address(tlm::tlm_generic_payload &transaction) {
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
      // free allocated address range on read
      deallocate_dynamic_address(transaction, transaction.get_address(),
                                 transaction.get_data_length());
    } else if (write_op && ext->flit_id != -1) {
      // off-chip read response
      set_flit_address(transaction);
    } else if (write_op && ext->fixed_address) {
      // on-chip fixed write request
      allocated_ranges[address] = data_size;
    } else if (write_op && !ext->fixed_address) {
      // on-chip dynamic write request
      uint32_t dynamic_address =
          allocate_dynamic_address(transaction, true, data_size);
      transaction.set_address(dynamic_address);
    }
  } else {
    if (read_op) {
      // off-chip read request
      transaction.set_address(address + offchip_base_address);
      // free allocated address range on read
      deallocate_dynamic_address(transaction, transaction.get_address(),
                                 transaction.get_data_length());
      // for read response: source becomes destination
      static_cast<ChipletPayload *>(&transaction)
          ->set_destination_id(ext->source_id);
    } else if (write_op && ext->fixed_address) {
      // off-chip fixed write request
      transaction.set_address(address + offchip_base_address);
      allocated_ranges[address + offchip_base_address] = data_size;
    } else if (write_op && !ext->fixed_address) {
      // off-chip dynamic write request
      set_flit_address(transaction);
    }
  }
}

void MemoryController::set_flit_address(tlm_generic_payload &transaction) {
  ChipletExtension *ext;
  transaction.get_extension(ext);

  int request_id = ext->request_id;
  int source_id = ext->source_id;
  int core_id = ext->core_id;

  FlitKey flit_key = {request_id, source_id, core_id};

  unsigned int flit_data_size = get_available_data_bytes_per_flit(transaction);
  unsigned int request_data_size = flit_data_size * ext->flit_count;

  if (ext->flit_id == 0) {
    // allocate dynamic address range with first flit
    uint32_t flit_base_address =
        allocate_dynamic_address(transaction, false, request_data_size);
    pending_flit_writes[flit_key] = flit_base_address;
    transaction.set_address(flit_base_address);
  } else {
    // increment dynamic address for upcoming flits
    auto it = pending_flit_writes.find(flit_key);

    uint32_t flit_base_address = it->second;
    uint32_t flit_address = flit_base_address + ext->flit_id * flit_data_size;

    transaction.set_address(flit_address);

    if (ext->flit_id == ext->flit_count - 1) {
      // deallocate flit padding on last flit
      deallocate_dynamic_address(transaction,
                                 transaction.get_address() +
                                     transaction.get_data_length(),
                                 ext->flit_padding);
      // remove pending on last flit
      pending_flit_writes.erase(it);
    }
  }
}

uint32_t
MemoryController::allocate_dynamic_address(tlm_generic_payload &transaction,
                                           bool onchip, uint32_t size) {
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
    address = base_address;
  }

  SC_LOG_DEBUG(this, transaction,
               "Allocate: " << std::hex << address << " - " << address + size);

  allocated_ranges[address] = size;
  return address;
}

void MemoryController::deallocate_dynamic_address(
    tlm_generic_payload &transaction, uint32_t address, unsigned int size) {
  auto it = allocated_ranges.lower_bound(address);
  if (it != allocated_ranges.begin() &&
      (it == allocated_ranges.end() || it->first > address)) {
    --it;
  }

  if (it == allocated_ranges.end() || address < it->first) {
    SC_LOG_WARN(this, transaction,
                "Tried to deallocate an unallocated address range");
    return;
  }

  uint32_t start = it->first;
  uint32_t end = start + it->second;
  uint32_t new_start = address + size;
  uint32_t new_end = end;

  SC_LOG_DEBUG(this, transaction,
               "Deallocate: " << std::hex << start << " - " << end);

  allocated_ranges.erase(it);

  if (address > start) {
    // left part remains
    SC_LOG_DEBUG(this, transaction,
                 "Allocate: " << std::hex << start << " - " << address);
    allocated_ranges[start] = address - start;
  }

  if (new_start < new_end) {
    // right part remains
    SC_LOG_DEBUG(this, transaction,
                 "Allocate: " << std::hex << new_start << " - " << new_end);
    allocated_ranges[new_start] = new_end - new_start;
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
MemoryController::nb_transport_fw(tlm_generic_payload &transaction,
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
MemoryController::nb_transport_bw(tlm_generic_payload &transaction,
                                  tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received response from RAM");

    ram_transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}