#include "modules/Memory.h"

#include "logging.h"

Memory::Memory(sc_module_name name, unsigned int axi_width, unsigned int size)
    : sc_module(name), size(size), utilization_tracker(this->name()),
      tsocket("tsocket", *this, &Memory::nb_transport_fw,
              ARM::TLM::PROTOCOL_AXI4, axi_width),
      mem(size * 1024, 0), write_flags(mem.size(), false),
      offchip_base_address(size * 1024 / 2) {
  SC_METHOD(clock_posedge);
  sensitive << clock.pos();
  dont_initialize();

  SC_METHOD(clock_negedge);
  sensitive << clock.neg();
  dont_initialize();
}

void Memory::clock_posedge() {
  if (b_state == ACK)
    b_state = CLEAR;
  if (r_state == ACK)
    r_state = CLEAR;

  // --- READ request ---
  if (!r_outgoing && !ar_queue.empty()) {
    if (active_addr == UINT32_MAX) {
      set_active_address(*ar_queue.front());
    } else {
      r_outgoing = ar_queue.front();
      ar_queue.pop_front();

      r_beat_count = r_outgoing->get_beat_count();
      unsigned beats = r_beat_count;
      unsigned beat_bytes = r_outgoing->get_beat_data_length();
      ARM::AXI::Burst burst = r_outgoing->get_burst();

      size_t total_len = static_cast<size_t>(beats * beat_bytes);
      sc_assert(total_len == r_outgoing->get_data_length());

      std::vector<uint8_t> staging(total_len);
      for (unsigned i = 0; i < beats; ++i) {
        const uint32_t a =
            set_beat_address(active_addr, i, beat_bytes, beats, burst);
        std::memcpy(&staging[i * beat_bytes], &mem[a], beat_bytes);
      }

      r_outgoing->read_in(staging.data());
    }
  }

  // --- WRITE request ---
  else if (!b_outgoing && !aw_queue.empty()) {
    if (active_addr == UINT32_MAX) {
      set_active_address(*aw_queue.front());
    } else if (!w_queue.empty()) {
      b_outgoing = w_queue.front();
      aw_queue.pop_front();
      w_queue.pop_front();

      unsigned beats = b_outgoing->get_beat_count();
      unsigned beat_bytes = b_outgoing->get_beat_data_length();
      ARM::AXI::Burst burst = b_outgoing->get_burst();

      size_t total_len = static_cast<size_t>(beats * beat_bytes);
      sc_assert(total_len == b_outgoing->get_data_length());

      std::vector<uint8_t> staging(total_len);
      b_outgoing->write_out(staging.data());

      for (unsigned i = 0; i < beats; ++i) {
        const uint32_t a =
            set_beat_address(active_addr, i, beat_bytes, beats, burst);
        std::memcpy(&mem[a], &staging[i * beat_bytes], beat_bytes);
      }

      b_outgoing->unref();
    }
  }
}

void Memory::clock_negedge() {
  if (r_outgoing) {
    ARM::AXI::Phase phase =
        (r_beat_count == 1) ? ARM::AXI::R_VALID_LAST : ARM::AXI::R_VALID;

    r_state = REQ;
    tlm_sync_enum reply = tsocket.nb_transport_bw(*r_outgoing, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::R_READY);
      r_state = ACK;
      r_beat_count--;
      if (r_beat_count == 0) {
        r_outgoing->unref();
        r_outgoing = nullptr;
        active_txn = false;
        active_addr = UINT32_MAX;
      }
    }
  }

  if (b_outgoing) {
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;

    b_state = REQ;
    tlm_sync_enum reply = tsocket.nb_transport_bw(*b_outgoing, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::B_READY);
      b_state = ACK;
      b_outgoing->unref();
      b_outgoing = nullptr;
      active_txn = false;
      active_addr = UINT32_MAX;
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Memory::nb_transport_fw(ARM::AXI::Payload &payload,
                                      ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AR_VALID:
    if (active_txn)
      return TLM_ACCEPTED; // backpressure
    active_txn = true;
    ar_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::AR_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_READY:
    r_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::AW_VALID:
    if (active_txn)
      return TLM_ACCEPTED; // backpressure
    active_txn = true;
    aw_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::AW_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID:
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID_LAST:
    w_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::B_READY:
    b_state = ACK;
    return TLM_ACCEPTED;
  default:
    SC_REPORT_ERROR(name(), "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

// -------------------------------------------------------
// helper functions
// -------------------------------------------------------
void Memory::set_active_address(ARM::AXI::Payload &payload) {
  uint32_t address = payload.get_address();
  unsigned data_size = payload.get_data_length();

  const UserSignals user = UserSignals::decode(payload.user);

  bool is_onchip = user.destination == user.source;

  bool read_op = payload.get_command() == ARM::AXI::COMMAND_READ;
  bool write_op = payload.get_command() == ARM::AXI::COMMAND_WRITE;

  if (is_onchip) {
    if (read_op) {
      // on-chip read request
      // free allocated address range on read
      deallocate_dynamic_address(address, data_size);
      active_addr = address;
    } else if (write_op && user.flit_count == 0) {
      // off-chip read response
      active_addr = set_flit_address(payload);
    } else if (write_op && user.fixed_address) {
      // on-chip fixed write request
      allocated_ranges[address] = data_size;
      active_addr = address;
    } else if (write_op && !user.fixed_address) {
      // on-chip dynamic write request
      active_addr = allocate_dynamic_address(true, data_size);
    }
  } else {
    if (read_op) {
      // off-chip read request
      active_addr = address + offchip_base_address;
      // free allocated address range on read
      deallocate_dynamic_address(address + offchip_base_address, data_size);
    } else if (write_op && user.fixed_address) {
      // off-chip fixed write request
      active_addr = address + offchip_base_address;
      allocated_ranges[address + offchip_base_address] = data_size;
    } else if (write_op && !user.fixed_address) {
      // off-chip dynamic write request
      active_addr = set_flit_address(payload);
    }
  }
}

uint32_t Memory::set_flit_address(ARM::AXI::Payload &payload) {
  const UserSignals user = UserSignals::decode(payload.user);

  int request_id = payload.id;
  int source_id = user.source;
  int core_id = user.core;

  FlitKey flit_key = {request_id, source_id, core_id};

  if (flit_id == 0) {
    flit_data_size = payload.get_data_length();
    unsigned request_data_size = flit_data_size * user.flit_count;
    // allocate dynamic address range with first flit
    uint32_t flit_base_address =
        allocate_dynamic_address(false, request_data_size);
    pending_flit_writes[flit_key] = flit_base_address;
    flit_id += 1;
    return flit_base_address;
  } else {
    // increment dynamic address for upcoming flits
    auto it = pending_flit_writes.find(flit_key);

    uint32_t flit_base_address = it->second;
    uint32_t flit_address = flit_base_address + flit_id * flit_data_size;

    if (flit_id == user.flit_count - 1) {
      // deallocate flit padding on last flit
      deallocate_dynamic_address(flit_address + payload.get_data_length(),
                                 flit_data_size - payload.get_data_length());
      flit_id = 0;
      // remove pending on last flit
      pending_flit_writes.erase(it);
    } else {
      flit_id += 1;
    }

    return flit_address;
  }
}

uint32_t Memory::allocate_dynamic_address(bool onchip, unsigned length) {
  uint32_t base_address = onchip ? 0 : offchip_base_address;
  uint32_t max_address = onchip ? offchip_base_address : size * 1024;

  uint32_t address = base_address;
  for (const auto &[start, len] : allocated_ranges) {
    if (address + length <= start) {
      break;
    }
    address = start + len;
  }

  if (address + length > max_address) {
    SC_LOG_WARN(this, "Out of memory for dynamic address allocation");
    address = base_address;
  }

  SC_LOG_DEBUG(this, "Allocate: " << std::hex << address << " - "
                                  << address + length);

  allocated_ranges[address] = length;
  return address;
}

void Memory::deallocate_dynamic_address(uint32_t address, unsigned length) {
  auto it = allocated_ranges.lower_bound(address);
  if (it != allocated_ranges.begin() &&
      (it == allocated_ranges.end() || it->first > address)) {
    --it;
  }

  if (it == allocated_ranges.end() || address < it->first) {
    SC_LOG_WARN(this, "Tried to deallocate an unallocated address range");
    return;
  }

  uint32_t start = it->first;
  uint32_t end = start + it->second;
  uint32_t new_start = address + length;
  uint32_t new_end = end;

  SC_LOG_DEBUG(this, "Deallocate: " << std::hex << start << " - " << end);

  allocated_ranges.erase(it);

  if (address > start) {
    // left part remains
    SC_LOG_DEBUG(this, "Allocate: " << std::hex << start << " - " << address);
    allocated_ranges[start] = address - start;
  }

  if (new_start < new_end) {
    // right part remains
    SC_LOG_DEBUG(this,
                 "Allocate: " << std::hex << new_start << " - " << new_end);
    allocated_ranges[new_start] = new_end - new_start;
  }
}

uint32_t Memory::set_beat_address(uint32_t base, unsigned beat_idx,
                                  unsigned beat_bytes, unsigned beats,
                                  ARM::AXI::Burst burst) {
  switch (burst) {
  case ARM::AXI::BURST_FIXED:
    return base;
  case ARM::AXI::BURST_INCR:
    return base + static_cast<uint32_t>(beat_idx * beat_bytes);
  case ARM::AXI::BURST_WRAP: {
    const uint32_t burst_size =
        static_cast<uint32_t>(beat_bytes * beats); // wrap boundary
    const uint32_t aligned_base =
        (base / burst_size) * burst_size; // floor to boundary
    const uint32_t incr_address =
        base + static_cast<uint64_t>(beat_idx * beat_bytes);
    return aligned_base | (incr_address & (burst_size - 1));
  }
  default:
    // treat unknown as INCR (safe fallback)
    return base + static_cast<uint32_t>(beat_idx * beat_bytes);
  }
}

// -------------------------------------------------------
// debug functions
// -------------------------------------------------------
void Memory::report_usage() {
  size_t written_bytes =
      std::count(write_flags.begin(), write_flags.end(), true);

  std::cout << "  Used Bytes: " << written_bytes << " / " << mem.size() << " ("
            << (100.0 * written_bytes / mem.size()) << "%)\n";
}