#include "Memory.h"

static inline uint64_t axi_beat_addr(uint64_t base, unsigned beat_idx,
                                     unsigned beat_bytes, // 2^AxSIZE
                                     unsigned beats,      // AxLEN+1
                                     ARM::AXI::Burst burst) {
  switch (burst) {
  case ARM::AXI::BURST_FIXED:
    return base;
  case ARM::AXI::BURST_INCR:
    return base + static_cast<uint64_t>(beat_idx) * beat_bytes;
  case ARM::AXI::BURST_WRAP: {
    const uint64_t burst_size =
        static_cast<uint64_t>(beat_bytes) * beats; // wrap boundary
    const uint64_t aligned_base =
        (base / burst_size) * burst_size; // floor to boundary
    const uint64_t incr_addr =
        base + static_cast<uint64_t>(beat_idx) * beat_bytes;
    return aligned_base | (incr_addr & (burst_size - 1));
  }
  default:
    // treat unknown as INCR (safe fallback)
    return base + static_cast<uint64_t>(beat_idx) * beat_bytes;
  }
}

void Memory::set_address(ARM::AXI::Payload &payload) {
  uint32_t address = payload.get_base_address();
  unsigned int data_size = payload.get_data_length();

  bool is_onchip = true;

  bool read_op = payload.get_command() == ARM::AXI::COMMAND_READ;
  bool write_op = payload.get_command() == ARM::AXI::COMMAND_WRITE;

  if (is_onchip) {
    if (read_op) {
      // on-chip read request
      // free allocated address range on read
      deallocate_dynamic_address(payload, address, data_size);
      // } else if (write_op && ext->flit_id != -1) {
      //   // off-chip read response
      //   set_flit_address(transaction);
    } else if (write_op) { //&& ext->fixed_address) {
      // on-chip fixed write request
      allocated_ranges[address] = data_size;
    }
    // } else if (write_op && !ext->fixed_address) {
    //   // on-chip dynamic write request
    //   uint32_t dynamic_address =
    //       allocate_dynamic_address(transaction, true, data_size);
    //   transaction.set_address(dynamic_address);
    // }
  }
  // } else {
  //   if (read_op) {
  //     // off-chip read request
  //     transaction.set_address(address + offchip_base_address);
  //     // free allocated address range on read
  //     deallocate_dynamic_address(transaction, transaction.get_address(),
  //                                transaction.get_data_length());
  //     // for read response: source becomes destination
  //     static_cast<ChipletPayload *>(&transaction)
  //         ->set_destination_id(ext->source_id);
  //   } else if (write_op && ext->fixed_address) {
  //     // off-chip fixed write request
  //     transaction.set_address(address + offchip_base_address);
  //     allocated_ranges[address + offchip_base_address] = data_size;
  //   } else if (write_op && !ext->fixed_address) {
  //     // off-chip dynamic write request
  //     set_flit_address(transaction);
  //   }
  // }
}

uint32_t Memory::allocate_dynamic_address(ARM::AXI::Payload &payload,
                                          bool onchip, uint32_t size) {
  uint32_t base_address = onchip ? 0 : offchip_base_address;
  uint32_t max_address = onchip ? offchip_base_address : size * 1024;

  uint32_t address = base_address;
  for (const auto &[start, len] : allocated_ranges) {
    if (address + size <= start) {
      break;
    }
    address = start + len;
  }

  if (address + size > max_address) {
    SC_LOG_DEBUG_NO_TX(this, "Out of memory for dynamic address allocation");
    address = base_address;
  }

  SC_LOG_DEBUG_NO_TX(this, "Allocate: " << std::hex << address << " - "
                                        << address + size);

  allocated_ranges[address] = size;
  return address;
}

void Memory::deallocate_dynamic_address(ARM::AXI::Payload &payload,
                                        uint32_t address, unsigned int size) {
  auto it = allocated_ranges.lower_bound(address);
  if (it != allocated_ranges.begin() &&
      (it == allocated_ranges.end() || it->first > address)) {
    --it;
  }

  if (it == allocated_ranges.end() || address < it->first) {
    SC_LOG_DEBUG_NO_TX(this,
                       "Tried to deallocate an unallocated address range");
    return;
  }

  uint32_t start = it->first;
  uint32_t end = start + it->second;
  uint32_t new_start = address + size;
  uint32_t new_end = end;

  SC_LOG_DEBUG_NO_TX(this, "Deallocate: " << std::hex << start << " - " << end);

  allocated_ranges.erase(it);

  if (address > start) {
    // left part remains
    SC_LOG_DEBUG_NO_TX(this, std::hex << start << " - " << address);
    allocated_ranges[start] = address - start;
  }

  if (new_start < new_end) {
    // right part remains
    SC_LOG_DEBUG_NO_TX(this, "Allocate: " << std::hex << new_start << " - "
                                          << new_end);
    allocated_ranges[new_start] = new_end - new_start;
  }
}

Memory::Memory(sc_module_name name, unsigned int size)
    : sc_module(name), size(size), utilization_tracker(this->name()),
      b_state(CLEAR), r_state(CLEAR), b_outgoing(nullptr), r_outgoing(nullptr),
      tsocket("target", *this, &Memory::nb_transport_fw,
              ARM::TLM::PROTOCOL_AXI4, 32),
      clock("clock"), mem(size * 1024, 0), write_flags(mem.size(), false),
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

  // only proceed if no transaction is active
  if (!active_txn) {
    // handle read
    if (!r_outgoing && !ar_queue.empty()) {
      r_outgoing = ar_queue.front();
      ar_queue.pop_front();

      if (!addr_phase_pending) {
        // first cycle: set address
        set_address(*r_outgoing);
        addr_phase_pending = true;
      } else {
        // second cycle: start transaction
        addr_phase_pending = false;
        active_txn = true;

        r_beat_count = r_outgoing->get_beat_count();
        const unsigned beats = r_beat_count; // AxLEN+1
        const unsigned beat_bytes = 4;       // 2^AxSIZE
        const uint64_t base = r_outgoing->get_base_address();
        const auto burst = r_outgoing->get_burst();

        const size_t total_len = static_cast<size_t>(beats) * beat_bytes;
        sc_assert(total_len == r_outgoing->get_data_length());

        std::vector<uint8_t> staging(total_len);
        for (unsigned i = 0; i < beats; ++i) {
          const uint64_t a = axi_beat_addr(base, i, beat_bytes, beats, burst);
          std::memcpy(&staging[i * beat_bytes], &mem[a], beat_bytes);
        }

        r_outgoing->read_in(staging.data());
      }
    }

    // handle write
    else if (!b_outgoing && !aw_queue.empty() && !w_queue.empty()) {
      sc_assert(aw_queue.front() == w_queue.front());
      b_outgoing = w_queue.front();
      aw_queue.pop_front();
      w_queue.pop_front();

      if (!addr_phase_pending) {
        // first cycle: set address
        set_address(*b_outgoing);
        addr_phase_pending = true;
      } else {
        // second cycle: accept transaction
        addr_phase_pending = false;
        active_txn = true;

        b_outgoing->set_resp(ARM::AXI::RESP_OKAY);

        uint64_t addr = b_outgoing->get_base_address();
        b_outgoing->write_out(&mem[addr]);

        b_outgoing->unref();
      }
    }
  }
}

void Memory::clock_negedge() {
  if (r_state == CLEAR && r_outgoing) {
    ARM::AXI::Phase phase = ARM::AXI::R_VALID;

    r_beat_count--;
    if (r_beat_count == 0)
      phase = ARM::AXI::R_VALID_LAST;

    r_state = REQ;
    tlm_sync_enum reply = tsocket.nb_transport_bw(*r_outgoing, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::R_READY);
      r_state = ACK;
    }

    if (r_beat_count == 0) {
      r_outgoing->unref();
      r_outgoing = nullptr;
      active_txn = false;
    }
  }

  if (b_outgoing) {
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;

    b_state = REQ;
    tlm_sync_enum reply = tsocket.nb_transport_bw(*b_outgoing, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::B_READY);
      b_state = ACK;
    }

    b_outgoing->unref();
    b_outgoing = nullptr;
    active_txn = false;
  }
}

void Memory::report_usage() {
  size_t written_bytes =
      std::count(write_flags.begin(), write_flags.end(), true);

  std::cout << "  Used Bytes: " << written_bytes << " / " << mem.size() << " ("
            << (100.0 * written_bytes / mem.size()) << "%)\n";
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Memory::nb_transport_fw(ARM::AXI::Payload &payload,
                                      ARM::AXI::Phase &phase) {
  SC_LOG_DEBUG_NO_TX(this, "AXI TLM Protocol: " << phase_to_string(phase));

  switch (phase) {
  case ARM::AXI::AW_VALID:
    if (active_txn)
      return TLM_ACCEPTED; // backpressure
    aw_queue.push_back(&payload);
    payload.ref();
    // only return READY if address cycle already consumed
    if (addr_phase_pending)
      return TLM_ACCEPTED;
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
  case ARM::AXI::AR_VALID:
    if (active_txn)
      return TLM_ACCEPTED; // backpressure
    ar_queue.push_back(&payload);
    payload.ref();
    // only return READY if address cycle already consumed
    if (addr_phase_pending)
      return TLM_ACCEPTED;
    phase = ARM::AXI::AR_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_READY:
    r_state = ACK;
    return TLM_ACCEPTED;
  default:
    SC_REPORT_ERROR(name(), "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}