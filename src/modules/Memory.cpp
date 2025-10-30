#include "modules/Memory.h"

#include "logging.h"

Memory::Memory(sc_module_name name, YAML::Node config)
    : sc_module(name), axi_width(config["axi"]["width"].as<unsigned>()),
      size(config["ram"]["size"].as<unsigned>()),
      tsocket("tsocket", *this, &Memory::nb_transport_fw,
              ARM::TLM::PROTOCOL_AXI4, axi_width),
      mem(size * 1024, 0), mem_bitmap((mem.size() + 7) / 8, 0),
      offchip_base_address(size * 1024 / 2) {
  stats.register_utilization(this->name());

  SC_METHOD(clk_posedge);
  sensitive << clk.pos();
  dont_initialize();

  SC_METHOD(clk_negedge);
  sensitive << clk.neg();
  dont_initialize();
}

void Memory::end_of_simulation() {
  size_t used_bytes = 0;
  for (uint8_t byte : mem_bitmap)
    used_bytes += __builtin_popcount(byte);
  stats.set_value(this->name(), "size_bytes", size * 1024);
  stats.set_value(this->name(), "used_bytes", used_bytes);
}

void Memory::clk_posedge() {
  if (b_state == ACK) {
    b_state = CLEAR;
    b_outgoing->unref();
    b_outgoing = nullptr;
    mem_state = MemoryState::Idle;
    stats.set_idle(this->name());
  }

  if (r_state == ACK) {
    r_state = CLEAR;
    r_beat_count--;
    if (r_beat_count == 0) {
      r_outgoing->unref();
      r_outgoing = nullptr;
      mem_state = MemoryState::Idle;
      stats.set_idle(this->name());
    }
  }

  // Controller
  if (!aw_queue.empty() && mem_state == MemoryState::Idle) {
    mem_state = MemoryState::WriteSet;
    stats.set_active(this->name());
  } else if (!ar_queue.empty() && mem_state == MemoryState::Idle) {
    mem_state = MemoryState::ReadSet;
    stats.set_active(this->name());
  }

  switch (mem_state) {
  case MemoryState::WriteSet: {
    set_active_address(*aw_queue.front());
    ARM::AXI::Phase phase = ARM::AXI::AW_READY;
    tsocket.nb_transport_bw(*aw_queue.front(), phase);
    mem_state = MemoryState::WriteAccess;
    break;
  }
  case MemoryState::WriteAccess: {
    if (!w_queue.empty()) {
      b_outgoing = w_queue.front();
      aw_queue.pop_front();
      w_queue.pop_front();

      unsigned beats = b_outgoing->get_beat_count();
      unsigned beat_bytes = b_outgoing->get_beat_data_length();
      ARM::AXI::Burst burst = b_outgoing->get_burst();

      std::vector<uint8_t> buffer(b_outgoing->get_data_length());
      b_outgoing->write_out(buffer.data());

      for (unsigned i = 0; i < beats; ++i) {
        const uint32_t a =
            set_beat_address(active_addr, i, beat_bytes, beats, burst);
        std::memcpy(&mem[a], &buffer[i * beat_bytes], beat_bytes);
        // Mark bytes as used
        for (unsigned b = 0; b < beat_bytes; ++b)
          mem_bitmap[(a + b) / 8] |= (1 << ((a + b) % 8));
      }

      mem_state = MemoryState::WriteResponse;
    }
    break;
  }
  case MemoryState::ReadSet: {
    set_active_address(*ar_queue.front());
    ARM::AXI::Phase phase = ARM::AXI::AR_READY;
    tsocket.nb_transport_bw(*ar_queue.front(), phase);
    mem_state = MemoryState::ReadAccess;
    break;
  }
  case MemoryState::ReadAccess: {
    if (!ar_queue.empty()) {
      r_outgoing = ar_queue.front();
      ar_queue.pop_front();

      r_beat_count = r_outgoing->get_beat_count();
      unsigned beats = r_beat_count;
      unsigned beat_bytes = r_outgoing->get_beat_data_length();
      ARM::AXI::Burst burst = r_outgoing->get_burst();

      std::vector<uint8_t> buffer(r_outgoing->get_data_length());

      for (unsigned i = 0; i < beats; ++i) {
        const uint32_t a =
            set_beat_address(active_addr, i, beat_bytes, beats, burst);
        std::memcpy(&buffer[i * beat_bytes], &mem[a], beat_bytes);
      }

      r_outgoing->read_in(buffer.data());

      mem_state = MemoryState::ReadResponse;
    }
    break;
  }
  default:
    break;
  }
}

void Memory::clk_negedge() {
  if (b_state == CLEAR && b_outgoing) {
    b_state = REQ;
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;
    tlm_sync_enum reply = tsocket.nb_transport_bw(*b_outgoing, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::B_READY,
                    "AXI TLM Protocol: Unexpected phase");
      b_state = ACK;
    }
  }

  if (r_state == CLEAR && r_outgoing) {
    r_state = REQ;
    ARM::AXI::Phase phase =
        (r_beat_count == 1) ? ARM::AXI::R_VALID_LAST : ARM::AXI::R_VALID;
    tlm_sync_enum reply = tsocket.nb_transport_bw(*r_outgoing, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::R_READY,
                    "AXI TLM Protocol: Unexpected phase");
      r_state = ACK;
    }
  }
}

// -------------------------------------------------------
// Transport Functions
// -------------------------------------------------------
tlm_sync_enum Memory::nb_transport_fw(ARM::AXI::Payload &payload,
                                      ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AR_VALID:
    ar_queue.push_back(&payload);
    payload.ref();
    return TLM_ACCEPTED;
  case ARM::AXI::R_READY:
    r_state = r_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::AW_VALID:
    aw_queue.push_back(&payload);
    payload.ref();
    return TLM_ACCEPTED;
  case ARM::AXI::W_VALID:
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID_LAST:
    w_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::B_READY:
    b_state = b_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

// -------------------------------------------------------
// Helper Functions
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
      // On-chip read transaction
      // Free allocated address range on read
      deallocate_dynamic_address(address, data_size);
      active_addr = address;
    } else if (write_op && user.fixed_address) {
      // On-chip fixed write transaction
      allocated_ranges[address] = data_size;
      active_addr = address;
    } else if (write_op && !user.fixed_address) {
      // On-chip dynamic write transaction
      active_addr = allocate_dynamic_address(true, data_size);
    }
  } else {
    if (read_op) {
      // Off-chip read transaction
      // Free allocated address range on read
      deallocate_dynamic_address(address + offchip_base_address, data_size);
      active_addr = address + offchip_base_address;
    } else if (write_op && user.fixed_address) {
      // Off-chip fixed write transaction
      allocated_ranges[address + offchip_base_address] = data_size;
      active_addr = address + offchip_base_address;
    } else if (write_op && !user.fixed_address) {
      // Off-chip dynamic write transaction
      active_addr = allocate_dynamic_address(false, data_size);
    }
  }

  payload.set_address(active_addr);
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
    // Left part remains
    SC_LOG_DEBUG(this, "Allocate: " << std::hex << start << " - " << address);
    allocated_ranges[start] = address - start;
  }

  if (new_start < new_end) {
    // Right part remains
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
    // Treat unknown as INCR (safe fallback)
    return base + static_cast<uint32_t>(beat_idx * beat_bytes);
  }
}